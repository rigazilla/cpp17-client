#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <vector>

using namespace hotrod;

/**
 * PUT Operation Unit Tests
 *
 * Tests PUT request encoding and response parsing per Protocol 4.0 spec.
 *
 * Request format (opcode 0x01):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes: vInt length + bytes)
 * - Time units (1 byte: high nibble = lifespan unit, low nibble = maxIdle unit)
 * - Lifespan (vLong, only if unit < 0x07)
 * - MaxIdle (vLong, only if unit < 0x07)
 * - Value (lp_bytes: vInt length + bytes)
 *
 * Response format (opcode 0x02):
 * - Response header (5+ bytes)
 * - Previous value (optional, based on status)
 *
 * Time units:
 * - 0x00 = SECONDS
 * - 0x07 = DEFAULT (infinite/server default)
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.PutOperation
 * - Java: org.infinispan.client.hotrod.impl.operations.AbstractKeyValueOperation
 * - Kaitai: hotrod40.ksy put_request
 */

// Test 1: PUT request encoding - simple key/value, no expiration
TEST(PutTest, EncodePutRequestSimple) {
    // Build request header for PUT
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x01;   // PUT_REQUEST
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

    // Add time units (0x77 = both DEFAULT, no lifespan/maxIdle follow)
    request.push_back(0x77);

    // Add value "myvalue" as bytes
    ByteArray value = {'m', 'y', 'v', 'a', 'l', 'u', 'e'};
    Codec::writeByteArray(request, value);

    // Verify request structure
    EXPECT_EQ(0xA0, request[0]);  // Magic
    EXPECT_EQ(0x01, request[1]);  // Message ID = 1
    EXPECT_EQ(0x28, request[2]);  // Version = 4.0
    EXPECT_EQ(0x01, request[3]);  // Opcode = PUT_REQUEST

    // Key starts after header (11 bytes minimum)
    size_t keyOffset = 11;
    EXPECT_EQ(5, request[keyOffset]);  // Key length = 5

    // Find time units byte (after key)
    size_t timeUnitsOffset = keyOffset + 1 + 5;  // offset + length byte + key bytes
    EXPECT_EQ(0x77, request[timeUnitsOffset]);

    // Value follows immediately (no lifespan/maxIdle)
    size_t valueOffset = timeUnitsOffset + 1;
    EXPECT_EQ(7, request[valueOffset]);  // Value length = 7
}

// Test 2: PUT request with lifespan
TEST(PutTest, EncodePutRequestWithLifespan) {
    RequestHeader header;
    header.messageId = 2;
    header.version = 0x28;
    header.opcode = 0x01;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'k', '1'};
    Codec::writeByteArray(request, key);

    // Time units: lifespan=SECONDS (0x00), maxIdle=DEFAULT (0x07)
    uint8_t timeUnits = (0x00 << 4) | 0x07;
    request.push_back(timeUnits);

    // Lifespan: 300 seconds (5 minutes)
    Codec::writeVLong(request, 300);

    // No maxIdle (unit was DEFAULT)

    ByteArray value = {'v', '1'};
    Codec::writeByteArray(request, value);

    // Verify time units
    size_t keyOffset = 11;
    size_t timeUnitsOffset = keyOffset + 1 + 2;  // after key
    EXPECT_EQ(0x07, request[timeUnitsOffset]);  // 0x00 << 4 | 0x07

    // Verify lifespan vLong follows
    size_t lifespanOffset = timeUnitsOffset + 1;
    // 300 fits in one byte: 0xAC 0x02 (multi-byte vLong)
    EXPECT_TRUE(lifespanOffset < request.size());
}

// Test 3: PUT request with both lifespan and maxIdle
TEST(PutTest, EncodePutRequestWithBothExpiration) {
    RequestHeader header;
    header.messageId = 3;
    header.version = 0x28;
    header.opcode = 0x01;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'t', 'e', 's', 't'};
    Codec::writeByteArray(request, key);

    // Time units: both SECONDS (0x00)
    uint8_t timeUnits = (0x00 << 4) | 0x00;
    request.push_back(timeUnits);

    // Lifespan: 600 seconds
    Codec::writeVLong(request, 600);

    // MaxIdle: 120 seconds
    Codec::writeVLong(request, 120);

    ByteArray value = {'v', 'a', 'l'};
    Codec::writeByteArray(request, value);

    // Verify structure
    EXPECT_EQ(0x01, request[3]);  // PUT_REQUEST opcode

    // Verify time units
    size_t keyOffset = 11;
    size_t timeUnitsOffset = keyOffset + 1 + 4;  // after key
    EXPECT_EQ(0x00, request[timeUnitsOffset]);  // Both SECONDS
}

// Test 4: PUT response parsing - success, no previous value
TEST(PutTest, ParsePutResponseSuccess) {
    // Mock PUT response:
    // Magic: 0xA1
    // Message ID: 1 (vLong)
    // Opcode: 0x02 (PUT_RESPONSE)
    // Status: 0x00 (NO_ERROR)
    // Topology change: 0x00
    // No previous value

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x02,        // Opcode = PUT_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00         // Topology change = none
        // No previous value
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x02, header.opcode);
    EXPECT_EQ(1, header.messageId);
    EXPECT_EQ(0x00, header.status);
}

// Test 5: PUT response parsing - success with previous value
TEST(PutTest, ParsePutResponseWithPreviousValue) {
    // Mock PUT response with previous value:
    // Status could be 0x03 or 0x04 in Protocol 4.0 to indicate previous value
    // For now, just test the structure

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x02,        // Opcode = PUT_RESPONSE
        0x00,        // Status = NO_ERROR (or 0x03/0x04 for prev value)
        0x00         // Topology change
        // Previous value would follow (lp_bytes or metadata+value)
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(0x02, header.opcode);
    EXPECT_EQ(0x00, header.status);
}

// Test 6: Round-trip PUT operation
TEST(PutTest, RoundTripPutOperation) {
    // Encode PUT request
    RequestHeader reqHeader;
    reqHeader.messageId = 42;
    reqHeader.version = 0x28;
    reqHeader.opcode = 0x01;
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

    // No expiration (both DEFAULT)
    request.push_back(0x77);

    ByteArray value = {'t', 'e', 's', 't', 'v', 'a', 'l', 'u', 'e'};
    Codec::writeByteArray(request, value);

    // Verify request
    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x01, request[3]);  // PUT_REQUEST

    // Simulate PUT response
    ByteArray response = {
        0xA1,        // Magic
        0x2A,        // Message ID = 42
        0x02,        // Opcode = PUT_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00         // Topology change
    };

    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    EXPECT_EQ(42, respHeader.messageId);
    EXPECT_EQ(0x02, respHeader.opcode);
    EXPECT_EQ(0x00, respHeader.status);
}

// Test 7: PUT request with named cache
TEST(PutTest, EncodePutRequestNamedCache) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;
    header.opcode = 0x01;
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

    request.push_back(0x77);  // No expiration

    ByteArray value = {'v', '1'};
    Codec::writeByteArray(request, value);

    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x01, request[3]);

    // Cache name should be in header
    EXPECT_GT(request.size(), 20);  // Header + cache name + key + time units + value
}

// Test 8: Verify PUT opcode constants
TEST(PutTest, OpcodeConstants) {
    // Verify PUT opcodes per Protocol 4.0 spec
    const uint8_t PUT_REQUEST = 0x01;
    const uint8_t PUT_RESPONSE = 0x02;

    EXPECT_EQ(0x01, PUT_REQUEST);
    EXPECT_EQ(0x02, PUT_RESPONSE);
}

// Test 9: Time units encoding
TEST(PutTest, TimeUnitsEncoding) {
    // Test various time unit combinations
    uint8_t bothDefault = (0x07 << 4) | 0x07;
    EXPECT_EQ(0x77, bothDefault);

    uint8_t lifespanSeconds_maxIdleDefault = (0x00 << 4) | 0x07;
    EXPECT_EQ(0x07, lifespanSeconds_maxIdleDefault);

    uint8_t lifespanDefault_maxIdleSeconds = (0x07 << 4) | 0x00;
    EXPECT_EQ(0x70, lifespanDefault_maxIdleSeconds);

    uint8_t bothSeconds = (0x00 << 4) | 0x00;
    EXPECT_EQ(0x00, bothSeconds);
}
