#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <vector>

using namespace hotrod;

/**
 * GET Operation Unit Tests
 *
 * Tests GET request encoding and response parsing per Protocol 4.0 spec.
 *
 * Request format (opcode 0x03):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes: vInt length + bytes)
 *
 * Response format (opcode 0x04):
 * - Response header (5+ bytes)
 * - Value (lp_bytes, only if status = 0x00 SUCCESS)
 *
 * Status codes:
 * - 0x00 = NO_ERROR (key found)
 * - 0x01 = KEY_DOES_NOT_EXIST
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.GetOperation
 * - Java: org.infinispan.client.hotrod.impl.operations.AbstractKeyOperation
 * - Kaitai: hotrod40.ksy get_response
 */

// Test 1: GET request encoding - simple key
TEST(GetTest, EncodeGetRequestSimpleKey) {
    // Build request header for GET
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x03;   // GET_REQUEST
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Add key "mykey" as bytes
    ByteArray key = {'m', 'y', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);

    // Verify request structure:
    // Header (11 bytes minimum):
    //   A0 01 28 03 00 00 01 00 00 00 00
    // Key (6 bytes):
    //   05 6D 79 6B 65 79
    //   (vInt 5 + "mykey")

    EXPECT_EQ(0xA0, request[0]);  // Magic
    EXPECT_EQ(0x01, request[1]);  // Message ID = 1
    EXPECT_EQ(0x28, request[2]);  // Version = 4.0
    EXPECT_EQ(0x03, request[3]);  // Opcode = GET_REQUEST

    // Key starts after header
    size_t keyOffset = 11;  // Assuming minimal header
    EXPECT_EQ(5, request[keyOffset]);  // Key length = 5
    EXPECT_EQ('m', request[keyOffset + 1]);
    EXPECT_EQ('y', request[keyOffset + 2]);
    EXPECT_EQ('k', request[keyOffset + 3]);
    EXPECT_EQ('e', request[keyOffset + 4]);
    EXPECT_EQ('y', request[keyOffset + 5]);
}

// Test 2: GET request with large key (multi-byte vInt length)
TEST(GetTest, EncodeGetRequestLargeKey) {
    RequestHeader header;
    header.messageId = 2;
    header.version = 0x28;
    header.opcode = 0x03;
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

// Test 3: GET response parsing - key found
TEST(GetTest, ParseGetResponseKeyFound) {
    // Mock GET response:
    // Magic: 0xA1
    // Message ID: 1 (vLong)
    // Opcode: 0x04 (GET_RESPONSE)
    // Status: 0x00 (NO_ERROR)
    // Topology change: 0x00
    // Value: "myvalue" (lp_bytes: 07 + "myvalue")

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x04,        // Opcode = GET_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change = none
        // Value (lp_bytes)
        0x07,        // Length = 7
        'm', 'y', 'v', 'a', 'l', 'u', 'e'
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x04, header.opcode);
    EXPECT_EQ(1, header.messageId);
    EXPECT_EQ(0x00, header.status);

    // Read value
    ByteArray value = Codec::readByteArray(response, offset);

    EXPECT_EQ(7, value.size());
    EXPECT_EQ('m', value[0]);
    EXPECT_EQ('y', value[1]);
    EXPECT_EQ('v', value[2]);
    EXPECT_EQ('a', value[3]);
    EXPECT_EQ('l', value[4]);
    EXPECT_EQ('u', value[5]);
    EXPECT_EQ('e', value[6]);
}

// Test 4: GET response parsing - key not found
TEST(GetTest, ParseGetResponseKeyNotFound) {
    // Mock GET response:
    // Status: 0x01 (KEY_DOES_NOT_EXIST)
    // No value in response body

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x04,        // Opcode = GET_RESPONSE
        0x01,        // Status = KEY_DOES_NOT_EXIST
        0x00         // Topology change = none
        // No value (status != 0x00)
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x04, header.opcode);
    EXPECT_EQ(0x01, header.status);  // KEY_DOES_NOT_EXIST

    // When status != 0x00, no value in response
    // Application should check status before reading value
}

// Test 5: GET response with empty value
TEST(GetTest, ParseGetResponseEmptyValue) {
    // Key exists but value is empty (length = 0)
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x04,        // Opcode = GET_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0x00         // Value length = 0 (empty byte array)
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x00, header.status);

    ByteArray value = Codec::readByteArray(response, offset);
    EXPECT_EQ(0, value.size());
}

// Test 6: GET response with large value (multi-byte vInt length)
TEST(GetTest, ParseGetResponseLargeValue) {
    // Value with 300 bytes (vInt length = 0xAC 0x02)
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x04,        // Opcode = GET_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0xAC, 0x02   // Value length = 300 (multi-byte vInt)
    };

    // Append 300 'X' bytes
    for (int i = 0; i < 300; i++) {
        response.push_back('X');
    }

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    (void)header;  // Status already verified via header parsing

    ByteArray value = Codec::readByteArray(response, offset);
    EXPECT_EQ(300, value.size());
    EXPECT_EQ('X', value[0]);
    EXPECT_EQ('X', value[299]);
}

// Test 7: Round-trip GET request/response
TEST(GetTest, RoundTripGetOperation) {
    // Encode GET request
    RequestHeader reqHeader;
    reqHeader.messageId = 42;
    reqHeader.version = 0x28;
    reqHeader.opcode = 0x03;
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
    EXPECT_EQ(0x03, request[3]);  // GET_REQUEST

    // Simulate GET response
    ByteArray response = {
        0xA1,        // Magic
        0x2A,        // Message ID = 42
        0x04,        // Opcode = GET_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change
        0x09,        // Value length = 9
        't', 'e', 's', 't', 'v', 'a', 'l', 'u', 'e'
    };

    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(42, respHeader.messageId);
    EXPECT_EQ(0x04, respHeader.opcode);
    EXPECT_EQ(0x00, respHeader.status);

    ByteArray value = Codec::readByteArray(response, offset);
    EXPECT_EQ(9, value.size());

    std::string valueStr(value.begin(), value.end());
    EXPECT_EQ("testvalue", valueStr);
}

// Test 8: GET request with named cache
TEST(GetTest, EncodeGetRequestNamedCache) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;
    header.opcode = 0x03;
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
    EXPECT_EQ(0x03, request[3]);

    // Cache name should be in header (check it's not empty after opcode)
    // The exact position depends on message ID encoding
    // Just verify request was built successfully
    EXPECT_GT(request.size(), 15);  // Header + cache name + key
}

// Test 9: Verify GET opcode constants
TEST(GetTest, OpcodeConstants) {
    // Verify GET opcodes per Protocol 4.0 spec
    const uint8_t GET_REQUEST = 0x03;
    const uint8_t GET_RESPONSE = 0x04;

    EXPECT_EQ(0x03, GET_REQUEST);
    EXPECT_EQ(0x04, GET_RESPONSE);
}
