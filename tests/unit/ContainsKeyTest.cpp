#include <gtest/gtest.h>
#include "hotrod/Codec.h"
#include "hotrod/HeaderCodec.h"
#include <cstdint>
#include <vector>

using namespace hotrod;

/**
 * CONTAINS_KEY Unit Tests
 *
 * Request format (opcode 0x0F) - key only, like GET:
 * - Protocol 4.0 header (11+ bytes)
 * - Key (lp_bytes)
 *
 * Response (0x10) carries no body — only the status. Java ContainsKeyOperation
 * returns !isNotExist(status) && isSuccess(status).
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.operations.ContainsKeyOperation
 */

namespace {

RequestHeader makeHeader() {
    RequestHeader h;
    h.messageId = 1;
    h.version = 0x28;      // Protocol 4.0
    h.opcode = 0x0F;       // CONTAINS_KEY_REQUEST
    h.cacheName = "";
    h.flags = 0;
    h.clientIntelligence = ClientIntelligence::BASIC;
    h.topologyId = 0;
    h.keyMediaType = 0;
    h.valueMediaType = 0;
    return h;
}

} // namespace

// Test 1: request is header + key only (no expiration, no value)
TEST(ContainsKeyTest, EncodeKeyOnly) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {'m', 'y', 'k', 'e', 'y'};
    Codec::writeByteArray(request, key);

    EXPECT_EQ(0xA0, request[0]);
    EXPECT_EQ(0x0F, request[3]);  // opcode

    size_t off = 11;
    EXPECT_EQ(key, Codec::readByteArray(request, off));
    EXPECT_EQ(off, request.size());  // nothing trailing the key
}

// Test 2: empty key still encodes as a zero-length lp_bytes
TEST(ContainsKeyTest, EncodeEmptyKey) {
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, makeHeader());

    ByteArray key = {};
    Codec::writeByteArray(request, key);

    size_t off = 11;
    EXPECT_EQ(key, Codec::readByteArray(request, off));
    EXPECT_EQ(off, request.size());
}

// Test 3: opcode constants
TEST(ContainsKeyTest, OpcodeConstants) {
    EXPECT_EQ(0x0F, Opcode::CONTAINS_KEY_REQUEST);
    EXPECT_EQ(0x10, Opcode::CONTAINS_KEY_RESPONSE);
}
