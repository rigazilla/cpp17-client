#pragma once

#include "Types.h"
#include "Authentication.h"
#include "HeaderCodec.h"  // ClientIntelligence
#include <cstdint>
#include <string>
#include <vector>

namespace hotrod {

class Connection;

/**
 * Transport seam for the SASL handshake.
 *
 * The Hot Rod wire has no total-length prefix on a response body, so a response
 * can only be consumed by reading its fields in order. The driver therefore
 * talks to the transport in typed reads rather than "read the whole body": the
 * production adapter (ConnectionSaslTransport) reads from a live blocking
 * Connection; unit tests supply a fake that serves canned frames. This keeps the
 * SCRAM state machine fully testable without a socket.
 */
class SaslTransport {
public:
    virtual ~SaslTransport() = default;

    // Send an AUTH-family request: full frame (header + body) under `opcode`.
    virtual void sendRequest(uint8_t opcode, const ByteArray& body) = 0;

    // Read the next response header (magic / messageId / opcode / status, and
    // consume any topology-change payload). Throws HotRodClientException on an
    // ERROR (0x50) frame or a non-NO_ERROR status. Returns the response opcode
    // for the caller to validate.
    virtual uint8_t readResponseHeader() = 0;

    // Typed body reads (mirror Connection's helpers).
    virtual uint8_t   readByte() = 0;
    virtual VInt      readVInt() = 0;
    virtual std::string readString() = 0;   // lp_string
    virtual ByteArray readByteArray() = 0;   // lp_bytes
};

/**
 * Production SaslTransport over a live, blocking Connection.
 *
 * Header parsing mirrors MultiplexedConnection::readLoop so the stream stays in
 * sync even if a topology update rides along on an AUTH response; the update is
 * consumed and discarded (auth runs per-connection before the read loop; fresh
 * topology is picked up on the first real operation).
 */
class ConnectionSaslTransport : public SaslTransport {
public:
    ConnectionSaslTransport(Connection* conn,
                            uint8_t protocolVersion,
                            ClientIntelligence intelligence,
                            const std::string& host,
                            uint16_t port);

    void sendRequest(uint8_t opcode, const ByteArray& body) override;
    uint8_t readResponseHeader() override;
    uint8_t readByte() override;
    VInt readVInt() override;
    std::string readString() override;
    ByteArray readByteArray() override;

private:
    Connection*        conn_;
    uint8_t            protocolVersion_;
    ClientIntelligence intelligence_;
    std::string        host_;
    uint16_t           port_;
    uint64_t           nextMessageId_ = 0;
};

/**
 * Drives a SASL SCRAM (SHA-1 / SHA-256 / SHA-512) handshake to completion over a
 * SaslTransport. Every failure — unsupported/unoffered mechanism, bad server
 * signature, or a server ERROR status — surfaces as HotRodClientException so
 * callers handle auth failures like any other connection failure.
 */
class SaslAuthenticator {
public:
    SaslAuthenticator(Authentication auth, std::string host, uint16_t port);

    // Perform the full handshake. Throws HotRodClientException on any failure.
    void authenticate(SaslTransport& transport);

private:
    Authentication auth_;
    std::string    host_;
    uint16_t       port_;
};

} // namespace hotrod
