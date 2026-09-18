#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <cstdint>
#include <vector>

using namespace hotrod;

/**
 * REMOVE_WITH_VERSION (removeIfUnmodified) Unit Tests
 *
 * Tests request encoding and the fixed-width version codec per Protocol 4.0.
 *
 * Request format (opcode 0x0D):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes: vInt length + bytes)
 * - Version (int64, 8 bytes, big-endian)
 *
 * Response format (opcode 0x0E): status only (no body unless FORCE_RETURN_VALUE).
 * Status codes: 0x00 removed, 0x01 not-executed (modified), 0x02 no-such-key,
 * 0x03/0x04 the *_WITH_PREVIOUS variants (body present).
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.RemoveIfUnmodifiedOperation
 *   (super.writeOperationRequest(key) then buf.writeLong(version))
 * - Kaitai: hotrod40.ksy
 */

// Test 1: writeLong emits 8 big-endian bytes, MSB first
TEST(RemoveWithVersionTest, WriteLongBigEndianOrder) {
    ByteArray buf;
    Codec::writeLong(buf, static_cast<int64_t>(1));

    ASSERT_EQ(8u, buf.size());
    EXPECT_EQ(0x00, buf[0]);
    EXPECT_EQ(0x00, buf[1]);
    EXPECT_EQ(0x00, buf[2]);
    EXPECT_EQ(0x00, buf[3]);
    EXPECT_EQ(0x00, buf[4]);
    EXPECT_EQ(0x00, buf[5]);
    EXPECT_EQ(0x00, buf[6]);
    EXPECT_EQ(0x01, buf[7]);
}

// Test 2: writeLong of a distinct-byte value pins the byte order exactly
TEST(RemoveWithVersionTest, WriteLongDistinctBytes) {
    ByteArray buf;
    Codec::writeLong(buf, static_cast<int64_t>(0x1122334455667788LL));

    ASSERT_EQ(8u, buf.size());
    EXPECT_EQ(0x11, buf[0]);
    EXPECT_EQ(0x22, buf[1]);
    EXPECT_EQ(0x33, buf[2]);
    EXPECT_EQ(0x44, buf[3]);
    EXPECT_EQ(0x55, buf[4]);
    EXPECT_EQ(0x66, buf[5]);
    EXPECT_EQ(0x77, buf[6]);
    EXPECT_EQ(0x88, buf[7]);
}

// Test 3: writeLong/readLong round-trip, including high-bit (negative) values
TEST(RemoveWithVersionTest, WriteReadLongRoundTrip) {
    const int64_t values[] = {
        0,
        1,
        -1,                                   // 0xFFFFFFFFFFFFFFFF
        0x7FFFFFFFFFFFFFFFLL,                 // INT64_MAX
        static_cast<int64_t>(0x8000000000000000ULL), // INT64_MIN
        0x1122334455667788LL,
    };

    for (int64_t v : values) {
        ByteArray buf;
        Codec::writeLong(buf, v);
        ASSERT_EQ(8u, buf.size());

        size_t offset = 0;
        int64_t decoded = Codec::readLong(buf, offset);
        EXPECT_EQ(v, decoded) << "round-trip failed for " << v;
        EXPECT_EQ(8u, offset);
    }
}

// Test 4: readLong throws when fewer than 8 bytes remain
TEST(RemoveWithVersionTest, ReadLongOutOfBounds) {
    ByteArray buf = {0x00, 0x00, 0x00};  // only 3 bytes
    size_t offset = 0;
    EXPECT_THROW(Codec::readLong(buf, offset), std::runtime_error);
}

// Test 5: full request encoding = header + key (lp_bytes) + 8-byte version
TEST(RemoveWithVersionTest, EncodeRequestKeyThenVersion) {
    RequestHeader header;
    header.messageId = 1;
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x0D;   // REMOVE_WITH_VERSION_REQUEST
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'v', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);
    Codec::writeLong(request, static_cast<int64_t>(0x0102030405060708LL));

    EXPECT_EQ(0xA0, request[0]);  // Magic
    EXPECT_EQ(0x01, request[1]);  // Message ID
    EXPECT_EQ(0x28, request[2]);  // Version 4.0
    EXPECT_EQ(0x0D, request[3]);  // Opcode = REMOVE_WITH_VERSION_REQUEST

    // Key + version live right after the 11-byte minimal header.
    size_t keyOffset = 11;
    EXPECT_EQ(0x04, request[keyOffset]);          // key length = 4
    EXPECT_EQ('v', request[keyOffset + 1]);
    EXPECT_EQ('k', request[keyOffset + 2]);
    EXPECT_EQ('e', request[keyOffset + 3]);
    EXPECT_EQ('y', request[keyOffset + 4]);

    // 8-byte version immediately follows the key bytes.
    size_t versionOffset = keyOffset + 1 + key.size();
    EXPECT_EQ(0x01, request[versionOffset + 0]);
    EXPECT_EQ(0x02, request[versionOffset + 1]);
    EXPECT_EQ(0x03, request[versionOffset + 2]);
    EXPECT_EQ(0x04, request[versionOffset + 3]);
    EXPECT_EQ(0x05, request[versionOffset + 4]);
    EXPECT_EQ(0x06, request[versionOffset + 5]);
    EXPECT_EQ(0x07, request[versionOffset + 6]);
    EXPECT_EQ(0x08, request[versionOffset + 7]);

    // Nothing trails the version.
    EXPECT_EQ(versionOffset + 8, request.size());
}

// Test 6: version round-trips through a full encoded request (decode side)
TEST(RemoveWithVersionTest, DecodeVersionFromRequest) {
    RequestHeader header;
    header.messageId = 7;
    header.version = 0x28;
    header.opcode = 0x0D;
    header.cacheName = "";
    header.flags = 0;
    header.clientIntelligence = ClientIntelligence::BASIC;
    header.topologyId = 0;
    header.keyMediaType = 0;
    header.valueMediaType = 0;

    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    ByteArray key = {'k'};
    const int64_t expectedVersion = -42;
    Codec::writeByteArray(request, key);
    Codec::writeLong(request, expectedVersion);

    size_t offset = 11;
    ByteArray decodedKey = Codec::readByteArray(request, offset);
    int64_t decodedVersion = Codec::readLong(request, offset);

    EXPECT_EQ(key, decodedKey);
    EXPECT_EQ(expectedVersion, decodedVersion);
}

// Test 7: opcode constants match the protocol spec
TEST(RemoveWithVersionTest, OpcodeConstants) {
    EXPECT_EQ(0x0D, Opcode::REMOVE_WITH_VERSION_REQUEST);
    EXPECT_EQ(0x0E, Opcode::REMOVE_WITH_VERSION_RESPONSE);
}
