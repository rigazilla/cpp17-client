#pragma once

#include "Types.h"
#include <string>
#include <map>

namespace hotrod {

// Forward declarations
class Connection;

// Protocol constants
namespace Protocol {
    const uint8_t REQUEST_MAGIC = 0xA0;
    const uint8_t RESPONSE_MAGIC = 0xA1;
    const uint8_t VERSION_30 = 0x1E;  // 30 decimal
    const uint8_t VERSION_31 = 0x1F;  // 31 decimal
    const uint8_t VERSION_40 = 0x28;  // 40 decimal
    const uint8_t VERSION_41 = 0x29;  // 41 decimal
}

// Client intelligence levels
enum class ClientIntelligence : uint8_t {
    BASIC = 0x01,
    TOPOLOGY_AWARE = 0x02,
    HASH_DISTRIBUTION_AWARE = 0x03
};

// Operation codes
namespace Opcode {
    const uint8_t PUT_REQUEST = 0x01;
    const uint8_t PUT_RESPONSE = 0x02;
    const uint8_t GET_REQUEST = 0x03;
    const uint8_t GET_RESPONSE = 0x04;
    const uint8_t REMOVE_REQUEST = 0x0B;
    const uint8_t REMOVE_RESPONSE = 0x0C;
    const uint8_t PING_REQUEST = 0x17;
    const uint8_t PING_RESPONSE = 0x18;
    const uint8_t AUTH_MECH_LIST_REQUEST = 0x21;
    const uint8_t AUTH_MECH_LIST_RESPONSE = 0x22;
    const uint8_t AUTH_REQUEST = 0x23;
    const uint8_t AUTH_RESPONSE = 0x24;
    const uint8_t ERROR_RESPONSE = 0x50;
}

// Status codes
namespace Status {
    const uint8_t NO_ERROR = 0x00;
    const uint8_t NOT_PUT_REMOVED_REPLACED = 0x01;
    const uint8_t KEY_DOES_NOT_EXIST = 0x02;
    const uint8_t SUCCESS_WITH_PREVIOUS = 0x03;
    const uint8_t INVALID_MAGIC_OR_MESSAGE_ID = 0x81;
    const uint8_t UNKNOWN_COMMAND = 0x82;
    const uint8_t UNKNOWN_VERSION = 0x83;
    const uint8_t SERVER_ERROR = 0x85;
    const uint8_t COMMAND_TIMEOUT = 0x86;
}

/**
 * Hot Rod Protocol 4.0 request header.
 *
 * Reference: Codec40.java, hotrod40.ksy (lines 315-354)
 */
struct RequestHeader {
    uint8_t magic = Protocol::REQUEST_MAGIC;
    VLong messageId = 0;
    uint8_t version = Protocol::VERSION_40;
    uint8_t opcode = 0;
    std::string cacheName;
    VInt flags = 0;
    ClientIntelligence clientIntelligence = ClientIntelligence::BASIC;
    VInt topologyId = 0;

    // Protocol 2.8+ (version >= 0x28): Media types
    uint8_t keyMediaType = 0;    // 0 = none
    uint8_t valueMediaType = 0;  // 0 = none

    // Protocol 4.0+ (version >= 40): Other params
    std::map<std::string, ByteArray> otherParams;
};


/**
 * Hot Rod Protocol 4.0 response header.
 *
 * Reference: Codec30.java, hotrod40.ksy (lines 360+)
 */
struct ResponseHeader {
    uint8_t magic = Protocol::RESPONSE_MAGIC;
    VLong messageId = 0;
    uint8_t opcode = 0;
    uint8_t status = 0;
    uint8_t topologyChangeMarker = 0;  // 0 = no change, 1 = changed
};

/**
 * Protocol header encoder/decoder.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.protocol.Codec40
 * - Kaitai: hotrod40.ksy (request_header, response_header)
 */
class HeaderCodec {
public:
    // Request header encoding (Protocol 4.0)
    static void writeRequestHeader(ByteArray& buffer, const RequestHeader& header);

    // Response header decoding
    static ResponseHeader readResponseHeader(const ByteArray& buffer, size_t& offset);
};

} // namespace hotrod
