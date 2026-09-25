#pragma once

#include "Types.h"
#include "HeaderCodec.h"  // Status:: constants
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace hotrod {

/**
 * Where an operation failed. This is the highest-value field on
 * HotRodClientException: it is what lets a caller decide whether retrying THIS
 * operation is safe (idempotency), independent of whether a retry is worthwhile.
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.1 / D4.
 */
enum class FailurePhase {
    BeforeSend,   // request never left the client (connect/write not started) — safe to retry
    AfterSend,    // request was (partially) sent, no usable response — AMBIGUOUS (may have applied)
    ServerError   // server replied with an ERROR (0x50) status
};

/**
 * Minimal, pure-value address of a server an operation was dispatched to.
 * Kept independent of ServerInfo/TopologyInfo so the exception stays trivially
 * copyable and free of client-internal coupling.
 */
struct ServerAddress {
    std::string host;
    uint16_t    port = 0;

    bool operator==(const ServerAddress& other) const {
        return host == other.host && port == other.port;
    }
};

/**
 * Typed exception for all Hot Rod client failures (Step 11a).
 *
 * Pure data: it carries the raw facts about a failure (phase, server status,
 * nodes tried) and nothing that captures client internals — so it is safe to
 * copy, log, and store. Whether a retry is *worthwhile* (isTransient) or the
 * outcome is *uncertain* (outcomeUncertain) is DERIVED via the free helpers
 * below, never baked in as a stored verdict.
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.1 / §3.3.
 */
class HotRodClientException : public std::runtime_error {
public:
    FailurePhase               phase;
    std::optional<uint8_t>     serverStatus;         // Hot Rod status / ERROR code, if any
    std::vector<ServerAddress> triedNodes;           // nodes attempted (union across retries)
    bool                       ownersExhausted;      // all owners of the key are in triedNodes

    HotRodClientException(const std::string& message,
                          FailurePhase phase,
                          std::optional<uint8_t> serverStatus = std::nullopt,
                          std::vector<ServerAddress> triedNodes = {},
                          bool ownersExhausted = false)
        : std::runtime_error(message)
        , phase(phase)
        , serverStatus(serverStatus)
        , triedNodes(std::move(triedNodes))
        , ownersExhausted(ownersExhausted)
    {}
};

// --- Classification helpers (advisory; see D4). Free functions, not verdicts. ---

/**
 * Is a server ERROR status worth retrying (i.e. NOT futile)?
 * Node-level failures (0x86/0x87/0x88) can clear on another node; request-level
 * failures (0x81–0x85) reproduce identically anywhere, so they are permanent.
 */
inline bool isTransientStatus(uint8_t status) {
    switch (status) {
        case Status::COMMAND_TIMEOUT:         // 0x86 busy/slow node (also outcome-uncertain)
        case Status::NODE_SUSPECTED:          // 0x87 node suspected
        case Status::ILLEGAL_LIFECYCLE_STATE: // 0x88 node starting/stopping
            return true;
        default:
            return false;                     // 0x81..0x85 and unknown → permanent
    }
}

/**
 * Axis 2 — "could a retry plausibly succeed?". Advisory only; the caller must
 * AND this with their own knowledge of whether THIS op is safe to retry
 * (see outcomeUncertain).
 */
inline bool isTransient(const HotRodClientException& e) {
    switch (e.phase) {
        case FailurePhase::BeforeSend: return true;   // nothing sent
        case FailurePhase::AfterSend:  return true;   // not futile, but ambiguous
        case FailurePhase::ServerError:
            return e.serverStatus.has_value() && isTransientStatus(*e.serverStatus);
    }
    return false;
}

/**
 * Axis 1 helper — "might the operation have applied despite the failure?".
 * Orthogonal to isTransient(). A command timeout is transient AND uncertain,
 * the same profile as a mid-flight drop (AfterSend).
 */
inline bool outcomeUncertain(const HotRodClientException& e) {
    return e.phase == FailurePhase::AfterSend
        || (e.phase == FailurePhase::ServerError &&
            e.serverStatus.has_value() && *e.serverStatus == Status::COMMAND_TIMEOUT);
}

} // namespace hotrod
