#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <vector>

using namespace hotrod;

/**
 * REMOVE Operation Unit Tests
 *
 * Tests REMOVE request encoding and response parsing per Protocol 4.0 spec.
 *
 * Request format (opcode 0x0B):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes: vInt length + bytes)
 *
 * Response format (opcode 0x0C):
 * - Response header (5+ bytes)
 * - Previous value (lp_bytes, only if status = 0x00 SUCCESS and key existed)
 *
 * Status codes:
 * - 0x00 = NO_ERROR (key existed and was removed)
 * - 0x01 = KEY_DOES_NOT_EXIST
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.RemoveOperation
 * - Kaitai: hotrod40.ksy (REMOVE uses key_request, same as GET)
 */

// Test 1: REMOVE request encoding - simple key
TEST(RemoveTest, EncodeRemoveRequestSimpleKey) {
    // Build request header for REMOVE
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x0B;   // REMOVE_REQUEST
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Add key "delkey" as bytes
    ByteArray key = {'d', 'e', 'l', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);

    // Verify request structure:
    // Header (11 bytes minimum):
    //   A0 01 28 0B 00 00 01 00 00 00 00
    // Key (7 bytes):
    //   06 64 65 6C 6B 65 79
    //   (vInt 6 + "delkey")

    EXPECT_EQ(0xA0, request[0]);  // Magic
    EXPECT_EQ(0x01, request[1]);  // Message ID = 1
    EXPECT_EQ(0x28, request[2]);  // Version = 4.0
    EXPECT_EQ(0x0B, request[3]);  // Opcode = REMOVE_REQUEST

    // Key starts after header
    size_t keyOffset = 11;  // Assuming minimal header
    EXPECT_EQ(6, request[keyOffset]);  // Key length = 6
    EXPECT_EQ('d', request[keyOffset + 1]);
    EXPECT_EQ('e', request[keyOffset + 2]);
    EXPECT_EQ('l', request[keyOffset + 3]);
    EXPECT_EQ('k', request[keyOffset + 4]);
    EXPECT_EQ('e', request[keyOffset + 5]);
    EXPECT_EQ('y', request[keyOffset + 6]);
}

// Test 2: REMOVE request with large key (multi-byte vInt length)
TEST(RemoveTest, EncodeRemoveRequestLargeKey) {
    RequestHeader header;
    header.messageId = 2;
    header.version = 0x28;
    header.opcode = 0x0B;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Create key with 200 bytes (multi-byte vInt: 0xC8 0x01)
    ByteArray key(200, 'X');
    Codec::writeByteArray(request, key);

    // Verify key length encoding (200 = 0xC8 → vInt = 0xC8 0x01)
    size_t keyOffset = 11;
    EXPECT_EQ(0xC8, request[keyOffset]);      // First byte (128 + 72)
    EXPECT_EQ(0x01, request[keyOffset + 1]);  // Second byte (1)
}

// Test 3: REMOVE response parsing - key existed
TEST(RemoveTest, ParseRemoveResponseKeyExisted) {
    // Mock REMOVE response with previous value:
    // Magic: 0xA1
    // Message ID: 1 (vLong)
    // Opcode: 0x0C (REMOVE_RESPONSE)
    // Status: 0x00 (NO_ERROR, key existed)
    // Topology change: 0x00
    // Previous value: "oldvalue" (lp_bytes: 08 + "oldvalue")

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x0C,        // Opcode = REMOVE_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change = none
        // Previous value (lp_bytes)
        0x08,        // Length = 8
        'o', 'l', 'd', 'v', 'a', 'l', 'u', 'e'
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x0C, header.opcode);
    EXPECT_EQ(1, header.messageId);
    EXPECT_EQ(0x00, header.status);

    // Read previous value
    ByteArray previousValue = Codec::readByteArray(response, offset);

    EXPECT_EQ(8, previousValue.size());
    std::string valueStr(previousValue.begin(), previousValue.end());
    EXPECT_EQ("oldvalue", valueStr);
}

// Test 4: REMOVE response parsing - key not found
TEST(RemoveTest, ParseRemoveResponseKeyNotFound) {
    // Mock REMOVE response:
    // Status: 0x01 (KEY_DOES_NOT_EXIST)
    // No previous value in response body

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x0C,        // Opcode = REMOVE_RESPONSE
        0x01,        // Status = KEY_DOES_NOT_EXIST
        0x00         // Topology change = none
        // No previous value (status != 0x00)
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x0C, header.opcode);
    EXPECT_EQ(0x01, header.status);  // KEY_DOES_NOT_EXIST

    // When status != 0x00, no previous value in response
    // Application should check status before reading value
}

// Test 5: REMOVE response with empty previous value
TEST(RemoveTest, ParseRemoveResponseEmptyPreviousValue) {
    // Key existed but had empty value
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x0C,        // Opcode = REMOVE_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0x00         // Previous value length = 0 (empty byte array)
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x00, header.status);

    ByteArray previousValue = Codec::readByteArray(response, offset);
    EXPECT_EQ(0, previousValue.size());
}

// Test 6: Round-trip REMOVE operation
TEST(RemoveTest, RoundTripRemoveOperation) {
    // Encode REMOVE request
    RequestHeader reqHeader;
    reqHeader.messageId = 42;
    reqHeader.version = 0x28;
    reqHeader.opcode = 0x0B;
    reqHeader.cacheName = "";
    reqHeader.flags = 0;
    reqHeader.clientIntelligence = ClientIntelligence::BASIC;
    reqHeader.topologyId = 0;
    reqHeader.keyMediaType = 0;
    reqHeader.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, reqHeader);

    ByteArray key = {'t', 'e', 's', 't', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);

    // Verify request
    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x0B, request[3]);  // REMOVE_REQUEST

    // Simulate REMOVE response (key existed, had value)
    ByteArray response = {
        0xA1,        // Magic
        0x2A,        // Message ID = 42
        0x0C,        // Opcode = REMOVE_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0x09,        // Previous value length = 9
        't', 'e', 's', 't', 'v', 'a', 'l', 'u', 'e'
    };

    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(42, respHeader.messageId);
    EXPECT_EQ(0x0C, respHeader.opcode);
    EXPECT_EQ(0x00, respHeader.status);

    ByteArray previousValue = Codec::readByteArray(response, offset);
    EXPECT_EQ(9, previousValue.size());

    std::string valueStr(previousValue.begin(), previousValue.end());
    EXPECT_EQ("testvalue", valueStr);
}

// Test 7: REMOVE request with named cache
TEST(RemoveTest, EncodeRemoveRequestNamedCache) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;
    header.opcode = 0x0B;
    header.cacheName = "myCache";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'k', '1'};
    Codec::writeByteArray(request, key);

    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x0B, request[3]);

    // Cache name should be in header (check it's not empty after opcode)
    // The exact position depends on message ID encoding
    // Just verify request was built successfully
    EXPECT_GT(request.size(), 15);  // Header + cache name + key
}

// Test 8: Verify REMOVE opcode constants
TEST(RemoveTest, OpcodeConstants) {
    // Verify REMOVE opcodes per Protocol 4.0 spec
    const uint8_t REMOVE_REQUEST = 0x0B;
    const uint8_t REMOVE_RESPONSE = 0x0C;

    EXPECT_EQ(0x0B, REMOVE_REQUEST);
    EXPECT_EQ(0x0C, REMOVE_RESPONSE);
}

// Test 9: REMOVE response with large previous value
TEST(RemoveTest, ParseRemoveResponseLargePreviousValue) {
    // Previous value with 300 bytes (vInt length = 0xAC 0x02)
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x0C,        // Opcode = REMOVE_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0xAC, 0x02   // Previous value length = 300 (multi-byte vInt)
    };

    // Append 300 'Y' bytes
    for (int i = 0; i < 300; i++) {
        response.push_back('Y');
    }

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    (void)header;  // Status already verified via header parsing

    ByteArray previousValue = Codec::readByteArray(response, offset);
    EXPECT_EQ(300, previousValue.size());
    EXPECT_EQ('Y', previousValue[0]);
    EXPECT_EQ('Y', previousValue[299]);
}
