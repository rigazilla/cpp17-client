#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <cstdint>
#include <vector>

using namespace hotrod;

/**
 * PUT_IF_ABSENT Unit Tests
 *
 * Request format (opcode 0x05) - identical layout to PUT:
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes)
 * - time_units (u1): HIGH nibble = lifespan unit, LOW nibble = maxIdle unit
 *   (0x07 = DEFAULT/infinite). Matches Java TimeUnitParam.encodeTimeUnits.
 * - lifespan (vLong) only if lifespan unit != DEFAULT
 * - max_idle (vLong) only if maxIdle unit != DEFAULT
 * - Value (lp_bytes)
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.PutIfAbsentOperation
 *   (extends AbstractKeyValueOperation, same encoder as PUT)
 */

namespace {

RequestHeader makeHeader() {
    RequestHeader h;
    h.messageId = 1;
    h.version = 0x28;      // Protocol 4.0
    h.opcode = 0x05;       // PUT_IF_ABSENT_REQUEST
    h.cacheName = "";
    h.flags = 0;
    h.clientIntelligence = ClientIntelligence::BASIC;
    h.topologyId = 0;
    h.keyMediaType = 0;
    h.valueMediaType = 0;
    return h;
}

} // namespace

// Test 1: default expiration (lifespan=0, maxIdle=0) -> time_units 0x77, no vLongs
TEST(PutIfAbsentTest, EncodeDefaultExpiration) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k', '1'};
    ByteArray value = {'v', '1', 'v'};
    Codec::writeByteArray(request, key);
    request.push_back(0x77);  // lifespan default, maxIdle default
    Codec::writeByteArray(request, value);

    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x05, request[3]);  // opcode

    size_t off = 11;
    EXPECT_EQ(key, Codec::readByteArray(request, off));
    EXPECT_EQ(0x77, request[off++]);  // both default -> no vLongs follow
    EXPECT_EQ(value, Codec::readByteArray(request, off));
    EXPECT_EQ(off, request.size());   // nothing trailing
}

// Test 2: lifespan set (high nibble = SECONDS 0x0), maxIdle default (low 0x7)
TEST(PutIfAbsentTest, EncodeLifespanOnly) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k'};
    ByteArray value = {'v'};
    Codec::writeByteArray(request, key);

    uint8_t timeUnits = (0x00 << 4) | 0x07;  // lifespan SECONDS, maxIdle DEFAULT
    request.push_back(timeUnits);
    Codec::writeVLong(request, 30);  // lifespan present
    Codec::writeByteArray(request, value);

    size_t off = 11;
    Codec::readByteArray(request, off);          // key
    uint8_t tu = request[off++];
    EXPECT_EQ(0x07, tu);
    EXPECT_EQ(0x00, (tu >> 4) & 0x0F);           // lifespan unit present
    EXPECT_EQ(0x07, tu & 0x0F);                  // maxIdle default
    EXPECT_EQ(30u, Codec::readVLong(request, off));
    EXPECT_EQ(value, Codec::readByteArray(request, off));
}

// Test 3: both lifespan and maxIdle set -> two vLongs, lifespan first
TEST(PutIfAbsentTest, EncodeBothExpirations) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'k'};
    ByteArray value = {'v'};
    Codec::writeByteArray(request, key);

    request.push_back((0x00 << 4) | 0x00);  // both SECONDS
    Codec::writeVLong(request, 30);  // lifespan
    Codec::writeVLong(request, 45);  // maxIdle
    Codec::writeByteArray(request, value);

    size_t off = 11;
    Codec::readByteArray(request, off);   // key
    EXPECT_EQ(0x00, request[off++]);      // time_units
    EXPECT_EQ(30u, Codec::readVLong(request, off));  // lifespan first
    EXPECT_EQ(45u, Codec::readVLong(request, off));  // maxIdle second
    EXPECT_EQ(value, Codec::readByteArray(request, off));
}

// Test 4: opcode constants
TEST(PutIfAbsentTest, OpcodeConstants) {
    EXPECT_EQ(0x05, Opcode::PUT_IF_ABSENT_REQUEST);
    EXPECT_EQ(0x06, Opcode::PUT_IF_ABSENT_RESPONSE);
}
