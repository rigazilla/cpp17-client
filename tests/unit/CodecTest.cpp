#include <gtest/gtest.h>
#include "hotrod/Codec.h"

using namespace hotrod;

// ============================================================================
// vInt Encoding Tests (based on test-vectors/step-01-primitives/vint-test-cases.json)
// ============================================================================

TEST(CodecTest, VInt_Zero) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 0);
    ASSERT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer[0], 0);
}

TEST(CodecTest, VInt_One) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);
    ASSERT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer[0], 1);
}

TEST(CodecTest, VInt_MaxSingleByte) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 127);
    ASSERT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer[0], 127);
}

TEST(CodecTest, VInt_FirstTwoBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 128);
    ByteArray expected = {128, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_255) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 255);
    ByteArray expected = {255, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_256) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 256);
    ByteArray expected = {128, 2};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_300) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 300);
    ByteArray expected = {172, 2};  // 0xAC 0x02
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_MaxTwoBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 16383);
    ByteArray expected = {255, 127};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_FirstThreeBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 16384);
    ByteArray expected = {128, 128, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_65535) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 65535);
    ByteArray expected = {255, 255, 3};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_1Million) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1000000);
    ByteArray expected = {192, 132, 61};  // 0xC0 0x84 0x3D
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_MaxThreeBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 2097151);
    ByteArray expected = {255, 255, 127};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_FirstFourBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 2097152);
    ByteArray expected = {128, 128, 128, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_MaxFourBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 268435455);
    ByteArray expected = {255, 255, 255, 127};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_FirstFiveBytes) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 268435456);
    ByteArray expected = {128, 128, 128, 128, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VInt_MaxUint32) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 4294967295U);
    ByteArray expected = {255, 255, 255, 255, 15};
    EXPECT_EQ(buffer, expected);
}

// ============================================================================
// vInt Decoding Tests (round-trip)
// ============================================================================

TEST(CodecTest, VInt_RoundTrip_Zero) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 0);
    size_t offset = 0;
    EXPECT_EQ(Codec::readVInt(buffer, offset), 0U);
    EXPECT_EQ(offset, 1);
}

TEST(CodecTest, VInt_RoundTrip_127) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 127);
    size_t offset = 0;
    EXPECT_EQ(Codec::readVInt(buffer, offset), 127U);
}

TEST(CodecTest, VInt_RoundTrip_300) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 300);
    size_t offset = 0;
    EXPECT_EQ(Codec::readVInt(buffer, offset), 300U);
}

TEST(CodecTest, VInt_RoundTrip_MaxUint32) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 4294967295U);
    size_t offset = 0;
    EXPECT_EQ(Codec::readVInt(buffer, offset), 4294967295U);
}

// ============================================================================
// vLong Encoding Tests
// ============================================================================

TEST(CodecTest, VLong_Zero) {
    ByteArray buffer;
    Codec::writeVLong(buffer, 0);
    ASSERT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer[0], 0);
}

TEST(CodecTest, VLong_127) {
    ByteArray buffer;
    Codec::writeVLong(buffer, 127);
    ASSERT_EQ(buffer.size(), 1);
    EXPECT_EQ(buffer[0], 127);
}

TEST(CodecTest, VLong_128) {
    ByteArray buffer;
    Codec::writeVLong(buffer, 128);
    ByteArray expected = {128, 1};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VLong_MaxUint32) {
    ByteArray buffer;
    Codec::writeVLong(buffer, 4294967295ULL);
    ByteArray expected = {255, 255, 255, 255, 15};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VLong_LargeValue) {
    ByteArray buffer;
    Codec::writeVLong(buffer, 1099511627776ULL);  // 2^40
    ByteArray expected = {128, 128, 128, 128, 128, 32};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, VLong_RoundTrip) {
    ByteArray buffer;
    VLong value = 1099511627776ULL;
    Codec::writeVLong(buffer, value);
    size_t offset = 0;
    EXPECT_EQ(Codec::readVLong(buffer, offset), value);
}

// ============================================================================
// String Encoding Tests (based on string-test-cases.json)
// ============================================================================

TEST(CodecTest, String_Empty) {
    ByteArray buffer;
    Codec::writeString(buffer, "");
    ByteArray expected = {0};  // vInt(0)
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, String_SingleChar) {
    ByteArray buffer;
    Codec::writeString(buffer, "a");
    ByteArray expected = {1, 97};  // vInt(1) + 'a'
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, String_Hello) {
    ByteArray buffer;
    Codec::writeString(buffer, "hello");
    ByteArray expected = {5, 104, 101, 108, 108, 111};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, String_Cafe_UTF8) {
    ByteArray buffer;
    Codec::writeString(buffer, "café");
    ByteArray expected = {5, 99, 97, 102, 195, 169};  // é = 0xC3 0xA9
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, String_ChineseChars) {
    ByteArray buffer;
    Codec::writeString(buffer, "hello世界");
    // Length: 11 bytes (5 ASCII + 6 for two 3-byte UTF-8 chars)
    ByteArray expected = {11, 104, 101, 108, 108, 111, 228, 184, 150, 231, 149, 140};
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, String_RoundTrip_Empty) {
    ByteArray buffer;
    Codec::writeString(buffer, "");
    size_t offset = 0;
    EXPECT_EQ(Codec::readString(buffer, offset), "");
}

TEST(CodecTest, String_RoundTrip_Hello) {
    ByteArray buffer;
    std::string original = "hello";
    Codec::writeString(buffer, original);
    size_t offset = 0;
    EXPECT_EQ(Codec::readString(buffer, offset), original);
}

TEST(CodecTest, String_RoundTrip_UTF8) {
    ByteArray buffer;
    std::string original = "café世界";
    Codec::writeString(buffer, original);
    size_t offset = 0;
    EXPECT_EQ(Codec::readString(buffer, offset), original);
}

// ============================================================================
// Byte Array Encoding Tests
// ============================================================================

TEST(CodecTest, ByteArray_Empty) {
    ByteArray buffer;
    ByteArray empty;
    Codec::writeByteArray(buffer, empty);
    ByteArray expected = {0};  // vInt(0)
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, ByteArray_SingleByte) {
    ByteArray buffer;
    ByteArray data = {42};
    Codec::writeByteArray(buffer, data);
    ByteArray expected = {1, 42};  // vInt(1) + byte
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, ByteArray_MultipleBytes) {
    ByteArray buffer;
    ByteArray data = {1, 2, 3, 4, 5};
    Codec::writeByteArray(buffer, data);
    ByteArray expected = {5, 1, 2, 3, 4, 5};  // vInt(5) + bytes
    EXPECT_EQ(buffer, expected);
}

TEST(CodecTest, ByteArray_RoundTrip) {
    ByteArray buffer;
    ByteArray original = {0xFF, 0x00, 0xAB, 0xCD};
    Codec::writeByteArray(buffer, original);
    size_t offset = 0;
    ByteArray decoded = Codec::readByteArray(buffer, offset);
    EXPECT_EQ(decoded, original);
}

TEST(CodecTest, ByteArray_LargeArray) {
    ByteArray buffer;
    ByteArray data(200, 0x42);  // 200 bytes of 0x42
    Codec::writeByteArray(buffer, data);
    size_t offset = 0;
    ByteArray decoded = Codec::readByteArray(buffer, offset);
    EXPECT_EQ(decoded, data);
}
