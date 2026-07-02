#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>

namespace hotrod {

// Type aliases for clarity
using ByteArray = std::vector<uint8_t>;
using VInt = uint32_t;
using VLong = uint64_t;

/**
 * Entry metadata returned with previous values in Protocol 4.0+
 *
 * Corresponds to entry_metadata structure in hotrod40.ksy
 */
struct EntryMetadata {
    // Timestamps (milliseconds since epoch)
    std::optional<int64_t> created;      // Present if lifespan is not infinite
    std::optional<int64_t> lastUsed;     // Present if maxIdle is not infinite

    // Expiration durations (seconds)
    std::optional<uint32_t> lifespan;    // Present if not infinite
    std::optional<uint32_t> maxIdle;     // Present if not infinite

    // Entry version for optimistic locking
    int64_t version;
};

/**
 * Entry with metadata and value (used for PUT/REMOVE with previousValue)
 */
struct EntryWithMetadata {
    EntryMetadata metadata;
    ByteArray value;
};

} // namespace hotrod
