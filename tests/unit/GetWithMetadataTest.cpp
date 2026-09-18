#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <vector>

using namespace hotrod;

/**
 * GET_WITH_METADATA Operation Unit Tests
 *
 * Tests GET_WITH_METADATA request encoding and response parsing per Protocol 4.0.
 *
 * Request format (opcode 0x1B):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes: vInt length + bytes)   [identical to GET]
 *
 * Response format (opcode 0x1C), on success (status 0x00):
 * - flag (1 byte): 0x01 = INFINITE_LIFESPAN, 0x02 = INFINITE_MAXIDLE
 * - created (int64, big-endian) + lifespan (vInt)   [only if lifespan not infinite]
 * - lastUsed (int64, big-endian) + maxIdle (vInt)    [only if maxIdle not infinite]
 * - version (int64, big-endian, 8 bytes, always present)
 * - value (lp_bytes)
 *
 * Status codes:
 * - 0x00 = NO_ERROR (key found)
 * - 0x01 / 0x02 = key not found
 *
 * The response body layout mirrors Connection::receiveMetadata() +
 * Connection::receiveByteArray(). These tests decode a crafted response with
 * the same byte layout to validate the wire format.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.GetWithMetadataOperation
 * - Kaitai: hotrod40.ksy entry_metadata (lines 498-516)
 */

namespace {

// Append an int64 as 8-byte big-endian (matches receiveMetadata's s8 reads).
void appendInt64BE(ByteArray& buf, int64_t value) {
    for (int i = 7; i >= 0; i--) {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

// Read an int64 as 8-byte big-endian (matches receiveMetadata's s8 reads).
int64_t readInt64BE(const ByteArray& buf, size_t& offset) {
    int64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | buf[offset++];
    }
    return value;
}

} // namespace

// Test 1: request encoding - simple key (identical to GET, opcode 0x1B)
TEST(GetWithMetadataTest, EncodeRequestSimpleKey) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = Opcode::GET_WITH_METADATA_REQUEST;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'m', 'y', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);

    EXPECT_EQ(0xA0, request[0]);  // Magic
    EXPECT_EQ(0x01, request[1]);  // Message ID = 1
    EXPECT_EQ(0x28, request[2]);  // Version = 4.0
    EXPECT_EQ(0x1B, request[3]);  // Opcode = GET_WITH_METADATA_REQUEST

    size_t keyOffset = 11;  // minimal header
    EXPECT_EQ(5, request[keyOffset]);  // Key length = 5
    EXPECT_EQ('m', request[keyOffset + 1]);
    EXPECT_EQ('y', request[keyOffset + 5]);
}

// Test 2: request encoding - large key (multi-byte vInt length)
TEST(GetWithMetadataTest, EncodeRequestLargeKey) {
    RequestHeader header;
    header.messageId = 2;
    header.version = 0x28;
    header.opcode = Opcode::GET_WITH_METADATA_REQUEST;
    header.clientIntelligence = ClientIntelligence::BASIC;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key(200, 'X');
    Codec::writeByteArray(request, key);

    size_t keyOffset = 11;
    EXPECT_EQ(0xC8, request[keyOffset]);      // 200 → 0xC8 0x01
    EXPECT_EQ(0x01, request[keyOffset + 1]);
}

// Test 3: response parsing - key found, infinite lifespan + infinite maxIdle
TEST(GetWithMetadataTest, ParseResponseInfiniteExpiration) {
    // flag 0x03 => both INFINITE_LIFESPAN and INFINITE_MAXIDLE:
    //   no created/lifespan, no lastUsed/maxIdle, only version + value
    const int64_t expectedVersion = 0x0102030405060708LL;

    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x1C,        // Opcode = GET_WITH_METADATA_RESPONSE
        0x00,        // Status = NO_ERROR
        0x00,        // Topology change = none
        0x03,        // flag = INFINITE_LIFESPAN | INFINITE_MAXIDLE
    };
    appendInt64BE(response, expectedVersion);  // version (always present)
    // value "meta" as lp_bytes
    ByteArray value = {'m', 'e', 't', 'a'};
    Codec::writeByteArray(response, value);

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    EXPECT_EQ(0x1C, header.opcode);
    EXPECT_EQ(0x00, header.status);

    // Parse body (mirrors receiveMetadata layout)
    uint8_t flag = response[offset++];
    EXPECT_EQ(0x03, flag);
    EXPECT_EQ(0x01, flag & 0x01);  // lifespan infinite -> created/lifespan absent
    EXPECT_EQ(0x02, flag & 0x02);  // maxIdle infinite  -> lastUsed/maxIdle absent

    int64_t version = readInt64BE(response, offset);
    EXPECT_EQ(expectedVersion, version);

    ByteArray parsedValue = Codec::readByteArray(response, offset);
    EXPECT_EQ("meta", std::string(parsedValue.begin(), parsedValue.end()));
    EXPECT_EQ(response.size(), offset);  // consumed entire body
}

// Test 4: response parsing - key found, finite lifespan + finite maxIdle
TEST(GetWithMetadataTest, ParseResponseFiniteExpiration) {
    const int64_t created  = 1700000000000LL;
    const uint32_t lifespan = 3600;
    const int64_t lastUsed = 1700000005000LL;
    const uint32_t maxIdle = 600;
    const int64_t version  = 42;

    ByteArray response = {
        0xA1, 0x01, 0x1C, 0x00, 0x00,
        0x00,        // flag = 0 => both lifespan and maxIdle finite
    };
    appendInt64BE(response, created);
    Codec::writeVInt(response, lifespan);
    appendInt64BE(response, lastUsed);
    Codec::writeVInt(response, maxIdle);
    appendInt64BE(response, version);
    ByteArray value = {'v', '1'};
    Codec::writeByteArray(response, value);

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    EXPECT_EQ(0x00, header.status);

    uint8_t flag = response[offset++];
    EXPECT_EQ(0x00, flag);
    ASSERT_EQ(0x00, flag & 0x01);  // lifespan present
    EXPECT_EQ(created, readInt64BE(response, offset));
    EXPECT_EQ(lifespan, Codec::readVInt(response, offset));
    ASSERT_EQ(0x00, flag & 0x02);  // maxIdle present
    EXPECT_EQ(lastUsed, readInt64BE(response, offset));
    EXPECT_EQ(maxIdle, Codec::readVInt(response, offset));
    EXPECT_EQ(version, readInt64BE(response, offset));

    ByteArray parsedValue = Codec::readByteArray(response, offset);
    EXPECT_EQ("v1", std::string(parsedValue.begin(), parsedValue.end()));
    EXPECT_EQ(response.size(), offset);
}

// Test 5: response parsing - mixed flags (finite lifespan, infinite maxIdle)
TEST(GetWithMetadataTest, ParseResponseMixedExpiration) {
    const int64_t created  = 1700000000000LL;
    const uint32_t lifespan = 120;
    const int64_t version  = 7;

    ByteArray response = {
        0xA1, 0x01, 0x1C, 0x00, 0x00,
        0x02,        // flag = INFINITE_MAXIDLE only (lifespan finite)
    };
    appendInt64BE(response, created);
    Codec::writeVInt(response, lifespan);
    // no lastUsed/maxIdle (maxIdle infinite)
    appendInt64BE(response, version);
    ByteArray value = {'x'};
    Codec::writeByteArray(response, value);

    size_t offset = 0;
    HeaderCodec::readResponseHeader(response, offset);
    uint8_t flag = response[offset++];
    ASSERT_EQ(0x00, flag & 0x01);  // lifespan present
    EXPECT_EQ(created, readInt64BE(response, offset));
    EXPECT_EQ(lifespan, Codec::readVInt(response, offset));
    ASSERT_EQ(0x02, flag & 0x02);  // maxIdle absent
    EXPECT_EQ(version, readInt64BE(response, offset));
    ByteArray parsedValue = Codec::readByteArray(response, offset);
    EXPECT_EQ("x", std::string(parsedValue.begin(), parsedValue.end()));
    EXPECT_EQ(response.size(), offset);
}

// Test 6: response parsing - key not found (status 0x02, no body)
TEST(GetWithMetadataTest, ParseResponseKeyNotFound) {
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID
        0x1C,        // Opcode
        0x02,        // Status = KEY_DOES_NOT_EXIST
        0x00         // Topology change = none
        // No body
    };

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    EXPECT_EQ(0x1C, header.opcode);
    EXPECT_EQ(0x02, header.status);
    EXPECT_EQ(response.size(), offset);  // nothing more to read
}

// Test 7: response parsing - key found, empty value
TEST(GetWithMetadataTest, ParseResponseEmptyValue) {
    const int64_t version = 99;
    ByteArray response = {
        0xA1, 0x01, 0x1C, 0x00, 0x00,
        0x03,        // flag = both infinite
    };
    appendInt64BE(response, version);
    response.push_back(0x00);  // value length = 0

    size_t offset = 0;
    HeaderCodec::readResponseHeader(response, offset);
    uint8_t flag = response[offset++];
    EXPECT_EQ(0x03, flag);
    EXPECT_EQ(version, readInt64BE(response, offset));
    ByteArray parsedValue = Codec::readByteArray(response, offset);
    EXPECT_EQ(0, parsedValue.size());
    EXPECT_EQ(response.size(), offset);
}

// Test 8: opcode constants
TEST(GetWithMetadataTest, OpcodeConstants) {
    EXPECT_EQ(0x1B, Opcode::GET_WITH_METADATA_REQUEST);
    EXPECT_EQ(0x1C, Opcode::GET_WITH_METADATA_RESPONSE);
}
