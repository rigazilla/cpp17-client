#include <gtest/gtest.h>
#include "hotrod/HeaderCodec.h"
#include "hotrod/Codec.h"
#include <vector>

using namespace hotrod;

/**
 * PING operation unit tests based on test vectors.
 *
 * Test vectors: hotrod-foundry/test-vectors/step-03-ping/ping-test-cases.json
 *
 * PING is the simplest operation - header only, no payload.
 */

// Test Case 1: Basic PING to default cache
TEST(PingTest, BasicPingRequest_Complete_Protocol40) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x17;   // PING_REQUEST
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;      // Required for Protocol 4.0
    header.valueMediaType = 0;    // Required for Protocol 4.0
    // otherParams empty, count = 0

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Expected: A0 01 28 17 00 00 01 00 00 00 00
    ByteArray expected = {0xA0, 0x01, 0x28, 0x17, 0x00, 0x00, 0x01, 0x00,
                          0x00, 0x00, 0x00};

    ASSERT_EQ(encoded, expected) << "PING request encoding failed";
    ASSERT_EQ(encoded.size(), 11) << "PING request should be 11 bytes (complete Protocol 4.0)";
}

// Test Case 2: PING response parsing
TEST(PingTest, BasicPingResponse) {
    // Response: A1 01 18 00 00
    ByteArray response = {0xA1, 0x01, 0x18, 0x00, 0x00};

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(header.messageId, 1);
    EXPECT_EQ(header.opcode, 0x18);  // PING_RESPONSE
    EXPECT_EQ(header.status, 0x00);  // NO_ERROR
    EXPECT_EQ(header.topologyChangeMarker, 0x00);
}

// Test Case 3: PING with named cache
TEST(PingTest, PingWithNamedCache) {
    RequestHeader header;
    header.messageId = 2;
    header.version = 0x28;
    header.opcode = 0x17;
    header.cacheName = "myCache";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Expected: A0 02 28 17 07 6D 79 43 61 63 68 65 00 01 00 00 00 00
    ByteArray expected = {0xA0, 0x02, 0x28, 0x17, 0x07, 0x6D, 0x79,
                          0x43, 0x61, 0x63, 0x68, 0x65, 0x00, 0x01,
                          0x00, 0x00, 0x00, 0x00};

    EXPECT_EQ(encoded, expected);
}

// Test Case 4: PING with large message ID
TEST(PingTest, PingWithLargeMessageId) {
    RequestHeader header;
    header.messageId = 1000000;
    header.version = 0x28;
    header.opcode = 0x17;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Expected: A0 C0 84 3D 28 17 00 00 01 00 00 00 00
    ByteArray expected = {0xA0, 0xC0, 0x84, 0x3D, 0x28, 0x17, 0x00, 0x00,
                          0x01, 0x00, 0x00, 0x00, 0x00};

    EXPECT_EQ(encoded, expected);
}

// Test Case 5: PING with topology awareness
TEST(PingTest, PingWithTopologyAwareness) {
    RequestHeader header;
    header.messageId = 10;
    header.version = 0x28;
    header.opcode = 0x17;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::TOPOLOGY_AWARE;
    header.topologyId = 5;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Expected: A0 0A 28 17 00 00 02 05 00 00 00
    ByteArray expected = {0xA0, 0x0A, 0x28, 0x17, 0x00, 0x00, 0x02, 0x05,
                          0x00, 0x00, 0x00};

    EXPECT_EQ(encoded, expected);
}

// Test Case 6: PING with hash awareness
TEST(PingTest, PingWithHashAwareness) {
    RequestHeader header;
    header.messageId = 15;
    header.version = 0x28;
    header.opcode = 0x17;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::HASH_DISTRIBUTION_AWARE;
    header.topologyId = 10;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Expected: A0 0F 28 17 00 00 03 0A 00 00 00
    ByteArray expected = {0xA0, 0x0F, 0x28, 0x17, 0x00, 0x00, 0x03, 0x0A,
                          0x00, 0x00, 0x00};

    EXPECT_EQ(encoded, expected);
}

// Test Case 7: PING response with matching message ID
TEST(PingTest, PingResponseMessageIdMatch) {
    // Request message ID: 1000000 (vLong: C0 84 3D)
    // Response: A1 C0 84 3D 18 00 00
    ByteArray response = {0xA1, 0xC0, 0x84, 0x3D, 0x18, 0x00, 0x00};

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(header.messageId, 1000000);
    EXPECT_EQ(header.opcode, 0x18);
    EXPECT_EQ(header.status, 0x00);
}

// Test Case 8: Verify Protocol 4.0 requires 11 bytes minimum
TEST(PingTest, Protocol40MinimumSize) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x17;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray encoded;
    HeaderCodec::writeRequestHeader(encoded, header);

    // Protocol 4.0 PING (no cache name) MUST be 11 bytes
    // Missing keyMediaType, valueMediaType, or otherParamCount causes server timeout
    ASSERT_EQ(encoded.size(), 11)
        << "Protocol 4.0 PING must include all 11 fields";
}

// Test Case 9: Round-trip test
TEST(PingTest, RoundTrip) {
    // Create request
    RequestHeader request;
    request.messageId = 42;
    request.version = 0x28;
    request.opcode = 0x17;
    request.cacheName = "testCache";
    request.flags = 0;
    request.clientIntelligence = ClientIntelligence::HASH_DISTRIBUTION_AWARE;
    request.topologyId = 7;
    request.keyMediaType = 0;
    request.valueMediaType = 0;

    ByteArray requestBytes;
    HeaderCodec::writeRequestHeader(requestBytes, request);

    // Simulate response
    ByteArray responseBytes;
    responseBytes.push_back(0xA1);  // Response magic

    // Copy message ID from request (vLong encoding)
    size_t offset = 1;  // Skip request magic
    VLong msgId = Codec::readVLong(requestBytes, offset);
    ByteArray msgIdBytes;
    Codec::writeVLong(msgIdBytes, msgId);
    responseBytes.insert(responseBytes.end(), msgIdBytes.begin(), msgIdBytes.end());

    responseBytes.push_back(0x18);  // PING_RESPONSE opcode
    responseBytes.push_back(0x00);  // NO_ERROR status
    responseBytes.push_back(0x00);  // No topology change

    // Parse response
    size_t respOffset = 0;
    ResponseHeader response = HeaderCodec::readResponseHeader(responseBytes, respOffset);

    EXPECT_EQ(response.messageId, request.messageId);
    EXPECT_EQ(response.opcode, 0x18);
    EXPECT_EQ(response.status, 0x00);
}
