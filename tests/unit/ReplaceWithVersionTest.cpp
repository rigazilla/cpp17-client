#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <cstdint>
#include <vector>

using namespace hotrod;

/**
 * REPLACE_WITH_VERSION (replaceIfUnmodified) Unit Tests
 *
 * Request format (opcode 0x09):
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes)
 * - time_units (u1): HIGH nibble = lifespan unit, LOW nibble = maxIdle unit
 *   (0x07 = DEFAULT/infinite). Matches Java TimeUnitParam.encodeTimeUnits:
 *   (encodedLifespan << 4) | encodedMaxIdle — same encoder PUT uses.
 * - lifespan (vLong) only if lifespan unit != DEFAULT
 * - max_idle (vLong) only if maxIdle unit != DEFAULT
 * - Version (int64, 8 bytes, big-endian / s8)
 * - Value (lp_bytes)
 *
 * NOTE: hotrod40.ksy `replace_if_unmodified_request` has the two expiration
 * nibbles flipped vs `put_request`/`expiration_params`; that is a schema bug.
 * Java uses one shared writeExpirationParams for PUT and replaceWithVersion,
 * so the encoding below (high=lifespan, low=maxIdle) is authoritative.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.ReplaceIfUnmodifiedOperation
 *   (writeArray(key); writeExpirationParams(...); writeLong(version); writeArray(value))
 */

namespace {

RequestHeader makeHeader() {
    RequestHeader h;
    h.messageId = 1;
    h.version = 0x28;      // Protocol 4.0
    h.opcode = 0x09;       // REPLACE_WITH_VERSION_REQUEST
    h.cacheName = "";
    h.flags = 0;
    h.clientIntelligence = ClientIntelligence::BASIC;
    h.topologyId = 0;
    h.keyMediaType = 0;
    h.valueMediaType = 0;
    return h;
}

} // namespace

// Test 1: default expiration (lifespan=0, maxIdle=0) -> time_units 0x77, no
// vLongs; then version (8 bytes) then value.
TEST(ReplaceWithVersionTest, EncodeDefaultExpiration) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k', '1'};
    ByteArray value = {'v', '1', 'v'};
    Codec::writeByteArray(request, key);

    request.push_back(0x77);  // high=0x7 lifespan default, low=0x7 maxIdle default
    Codec::writeLong(request, static_cast<int64_t>(0x1122334455667788LL));
    Codec::writeByteArray(request, value);

    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x09, request[3]);  // opcode

    size_t off = 11;
    ByteArray decKey = Codec::readByteArray(request, off);
    EXPECT_EQ(key, decKey);

    uint8_t timeUnits = request[off++];
    EXPECT_EQ(0x77, timeUnits);  // both default -> no vLongs follow

    int64_t version = Codec::readLong(request, off);
    EXPECT_EQ(0x1122334455667788LL, version);

    ByteArray decVal = Codec::readByteArray(request, off);
    EXPECT_EQ(value, decVal);
    EXPECT_EQ(off, request.size());  // nothing trailing
}

// Test 2: lifespan set (high nibble = SECONDS 0x0), maxIdle default (low 0x7)
TEST(ReplaceWithVersionTest, EncodeLifespanOnly) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k'};
    ByteArray value = {'v'};
    Codec::writeByteArray(request, key);

    uint8_t timeUnits = (0x00 << 4) | 0x07;  // lifespan SECONDS, maxIdle DEFAULT
    request.push_back(timeUnits);
    Codec::writeVLong(request, 30);  // lifespan present
    Codec::writeLong(request, static_cast<int64_t>(7));
    Codec::writeByteArray(request, value);

    size_t off = 11;
    Codec::readByteArray(request, off);          // key
    uint8_t tu = request[off++];
    EXPECT_EQ(0x07, tu);                          // high=0 lifespan, low=7 maxIdle
    EXPECT_EQ(0x00, (tu >> 4) & 0x0F);            // lifespan unit present
    EXPECT_EQ(0x07, tu & 0x0F);                   // maxIdle default

    uint64_t lifespan = Codec::readVLong(request, off);
    EXPECT_EQ(30u, lifespan);
    EXPECT_EQ(7, Codec::readLong(request, off));  // version follows lifespan
}

// Test 3: maxIdle set (low nibble = SECONDS 0x0), lifespan default (high 0x7)
TEST(ReplaceWithVersionTest, EncodeMaxIdleOnly) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k'};
    ByteArray value = {'v'};
    Codec::writeByteArray(request, key);

    uint8_t timeUnits = (0x07 << 4) | 0x00;  // lifespan DEFAULT, maxIdle SECONDS
    request.push_back(timeUnits);
    Codec::writeVLong(request, 45);  // maxIdle present
    Codec::writeLong(request, static_cast<int64_t>(9));
    Codec::writeByteArray(request, value);

    size_t off = 11;
    Codec::readByteArray(request, off);          // key
    uint8_t tu = request[off++];
    EXPECT_EQ(0x70, tu);                          // high=7 lifespan default, low=0 maxIdle
    EXPECT_EQ(0x07, (tu >> 4) & 0x0F);            // lifespan default
    EXPECT_EQ(0x00, tu & 0x0F);                   // maxIdle unit present

    uint64_t maxIdle = Codec::readVLong(request, off);
    EXPECT_EQ(45u, maxIdle);
    EXPECT_EQ(9, Codec::readLong(request, off));  // version follows maxIdle
}

// Test 4: both lifespan and maxIdle set -> two vLongs, lifespan first
TEST(ReplaceWithVersionTest, EncodeBothExpirations) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k'};
    ByteArray value = {'v'};
    Codec::writeByteArray(request, key);

    uint8_t timeUnits = (0x00 << 4) | 0x00;  // both SECONDS
    request.push_back(timeUnits);
    Codec::writeVLong(request, 30);  // lifespan
    Codec::writeVLong(request, 45);  // maxIdle
    Codec::writeLong(request, static_cast<int64_t>(-1));
    Codec::writeByteArray(request, value);

    size_t off = 11;
    Codec::readByteArray(request, off);   // key
    EXPECT_EQ(0x00, request[off++]);      // time_units
    EXPECT_EQ(30u, Codec::readVLong(request, off));  // lifespan first
    EXPECT_EQ(45u, Codec::readVLong(request, off));  // maxIdle second
    EXPECT_EQ(-1, Codec::readLong(request, off));    // version
    EXPECT_EQ(value, Codec::readByteArray(request, off));
}

// Test 5: opcode constants
TEST(ReplaceWithVersionTest, OpcodeConstants) {
    EXPECT_EQ(0x09, Opcode::REPLACE_WITH_VERSION_REQUEST);
    EXPECT_EQ(0x0A, Opcode::REPLACE_WITH_VERSION_RESPONSE);
}
