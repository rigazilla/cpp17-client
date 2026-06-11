#include <gtest/gtest.h>
#include "hotrod/HeaderCodec.h"

using namespace hotrod;

// ============================================================================
// Request Header Encoding Tests (Protocol 4.0 Complete Spec)
// ============================================================================

// PING request - minimal header with ALL Protocol 4.0 fields
TEST(HeaderCodecTest, RequestHeader_PING_Complete) {
    RequestHeader header;
    header.magic = Protocol::REQUEST_MAGIC;
    header.messageId = 1;
    header.version = Protocol::VERSION_40;  // 0x28 = 40
    header.opcode = Opcode::PING_REQUEST;   // 0x17 = 23
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    // Protocol 4.0 REQUIRED fields:
    header.keyMediaType = 0;    // no key type
    header.valueMediaType = 0;  // no value type
    // otherParams is empty, so count = 0

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    // Expected bytes for Protocol 4.0 PING:
    // 0xA0 (magic) + 0x01 (messageId vLong) + 0x28 (version) + 0x17 (opcode) +
    // 0x00 (empty cache name) + 0x00 (flags) + 0x01 (intelligence) + 0x00 (topology) +
    // 0x00 (key media type) + 0x00 (value media type) + 0x00 (other param count)
    ByteArray expected = {0xA0, 0x01, 0x28, 0x17, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// GET request with named cache and all Protocol 4.0 fields
TEST(HeaderCodecTest, RequestHeader_GET_NamedCache) {
    RequestHeader header;
    header.messageId = 2;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::GET_REQUEST;  // 0x03
    header.cacheName = "myCache";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    // 0xA0 + 0x02 + 0x28 + 0x03 + "myCache"(0x07+"myCache") + 0x00 + 0x01 + 0x00 + 0x00 + 0x00 + 0x00
    ByteArray expected = {0xA0, 0x02, 0x28, 0x03, 0x07, 0x6D, 0x79, 0x43, 0x61, 0x63, 0x68, 0x65,
                          0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// PUT request with flags and Protocol 4.0 fields
TEST(HeaderCodecTest, RequestHeader_PUT_WithFlags) {
    RequestHeader header;
    header.messageId = 3;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::PUT_REQUEST;  // 0x01
    header.cacheName = "testCache";
    header.flags = 1;  // FORCE_RETURN_VALUE
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x03, 0x28, 0x01, 0x09, 0x74, 0x65, 0x73, 0x74, 0x43, 0x61, 0x63, 0x68, 0x65,
                          0x01, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// Large message ID (vLong encoding)
TEST(HeaderCodecTest, RequestHeader_LargeMessageId) {
    RequestHeader header;
    header.messageId = 1000000;  // Encodes as 0xC0 0x84 0x3D
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::PING_REQUEST;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0xC0, 0x84, 0x3D, 0x28, 0x17, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// REMOVE request
TEST(HeaderCodecTest, RequestHeader_REMOVE) {
    RequestHeader header;
    header.messageId = 5;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::REMOVE_REQUEST;  // 0x0B
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x05, 0x28, 0x0B, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// Topology-aware client
TEST(HeaderCodecTest, RequestHeader_TopologyAware) {
    RequestHeader header;
    header.messageId = 10;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::GET_REQUEST;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::TOPOLOGY_AWARE;  // 0x02
    header.topologyId = 5;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x0A, 0x28, 0x03, 0x00, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// Hash-distribution-aware client
TEST(HeaderCodecTest, RequestHeader_HashAware) {
    RequestHeader header;
    header.messageId = 15;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::GET_REQUEST;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::HASH_DISTRIBUTION_AWARE;  // 0x03
    header.topologyId = 10;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x0F, 0x28, 0x03, 0x00, 0x00, 0x03, 0x0A, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// Media types (non-zero)
TEST(HeaderCodecTest, RequestHeader_WithMediaTypes) {
    RequestHeader header;
    header.messageId = 20;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::PUT_REQUEST;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 1;    // Some media type ID
    header.valueMediaType = 2;  // Some media type ID

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x14, 0x28, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x00};
    EXPECT_EQ(buffer, expected);
}

// AUTH_MECH_LIST request
TEST(HeaderCodecTest, RequestHeader_AuthMechList) {
    RequestHeader header;
    header.messageId = 1;
    header.version = Protocol::VERSION_40;
    header.opcode = Opcode::AUTH_MECH_LIST_REQUEST;  // 0x21
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray buffer;
    HeaderCodec::writeRequestHeader(buffer, header);

    ByteArray expected = {0xA0, 0x01, 0x28, 0x21, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(buffer, expected);
}

// ============================================================================
// Response Header Decoding Tests
// ============================================================================

// PING response - success
TEST(HeaderCodecTest, ResponseHeader_PING_Success) {
    ByteArray buffer = {0xA1, 0x01, 0x18, 0x00, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.magic, Protocol::RESPONSE_MAGIC);
    EXPECT_EQ(header.messageId, 1U);
    EXPECT_EQ(header.opcode, Opcode::PING_RESPONSE);
    EXPECT_EQ(header.status, Status::NO_ERROR);
    EXPECT_EQ(header.topologyChangeMarker, 0);
    EXPECT_EQ(offset, 5);  // All 5 bytes consumed
}

// GET response - key not found
TEST(HeaderCodecTest, ResponseHeader_GET_NotFound) {
    ByteArray buffer = {0xA1, 0x02, 0x04, 0x02, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 2U);
    EXPECT_EQ(header.opcode, Opcode::GET_RESPONSE);
    EXPECT_EQ(header.status, Status::KEY_DOES_NOT_EXIST);
    EXPECT_EQ(header.topologyChangeMarker, 0);
}

// PUT response - success with previous value
TEST(HeaderCodecTest, ResponseHeader_PUT_WithPrevious) {
    ByteArray buffer = {0xA1, 0x03, 0x02, 0x03, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 3U);
    EXPECT_EQ(header.opcode, Opcode::PUT_RESPONSE);
    EXPECT_EQ(header.status, Status::SUCCESS_WITH_PREVIOUS);
    EXPECT_EQ(header.topologyChangeMarker, 0);
}

// Response with topology change marker
TEST(HeaderCodecTest, ResponseHeader_TopologyChange) {
    ByteArray buffer = {0xA1, 0x05, 0x04, 0x00, 0x01};  // marker = 1
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 5U);
    EXPECT_EQ(header.opcode, Opcode::GET_RESPONSE);
    EXPECT_EQ(header.status, Status::NO_ERROR);
    EXPECT_EQ(header.topologyChangeMarker, 1);  // Topology changed!
}

// Large message ID in response
TEST(HeaderCodecTest, ResponseHeader_LargeMessageId) {
    ByteArray buffer = {0xA1, 0xC0, 0x84, 0x3D, 0x18, 0x00, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 1000000U);
    EXPECT_EQ(header.opcode, Opcode::PING_RESPONSE);
    EXPECT_EQ(header.status, Status::NO_ERROR);
}

// Error response - server error
TEST(HeaderCodecTest, ResponseHeader_ServerError) {
    ByteArray buffer = {0xA1, 0x10, 0x50, 0x85, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 16U);
    EXPECT_EQ(header.opcode, Opcode::ERROR_RESPONSE);
    EXPECT_EQ(header.status, Status::SERVER_ERROR);
}

// AUTH_MECH_LIST response
TEST(HeaderCodecTest, ResponseHeader_AuthMechList) {
    ByteArray buffer = {0xA1, 0x01, 0x22, 0x00, 0x00};
    size_t offset = 0;

    ResponseHeader header = HeaderCodec::readResponseHeader(buffer, offset);

    EXPECT_EQ(header.messageId, 1U);
    EXPECT_EQ(header.opcode, Opcode::AUTH_MECH_LIST_RESPONSE);
    EXPECT_EQ(header.status, Status::NO_ERROR);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

// Invalid magic byte
TEST(HeaderCodecTest, ResponseHeader_InvalidMagic) {
    ByteArray buffer = {0xFF, 0x01, 0x18, 0x00, 0x00};
    size_t offset = 0;

    EXPECT_THROW({
        HeaderCodec::readResponseHeader(buffer, offset);
    }, std::runtime_error);
}

// Buffer too short
TEST(HeaderCodecTest, ResponseHeader_BufferTooShort) {
    ByteArray buffer = {0xA1, 0x01};  // Incomplete
    size_t offset = 0;

    EXPECT_THROW({
        HeaderCodec::readResponseHeader(buffer, offset);
    }, std::runtime_error);
}

// Empty buffer
TEST(HeaderCodecTest, ResponseHeader_EmptyBuffer) {
    ByteArray buffer;
    size_t offset = 0;

    EXPECT_THROW({
        HeaderCodec::readResponseHeader(buffer, offset);
    }, std::runtime_error);
}
