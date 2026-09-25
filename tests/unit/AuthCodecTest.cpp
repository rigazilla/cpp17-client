/**
 * Unit tests for the SASL auth message body codec (AuthCodec.h).
 *
 * Verifies the wire layout against the authoritative Kaitai schema
 * (hotrod40.ksy): mech-list response = vint count + lp_string*; auth request =
 * lp_string mech + lp_bytes response; auth response = u1 completed + lp_bytes.
 */
#include <gtest/gtest.h>
#include "hotrod/AuthCodec.h"
#include "hotrod/Codec.h"

using namespace hotrod;

namespace {

// Build a mech-list response body: vint count followed by that many lp_strings.
ByteArray buildMechListBody(const std::vector<std::string>& mechs) {
    ByteArray body;
    Codec::writeVInt(body, static_cast<VInt>(mechs.size()));
    for (const auto& m : mechs) {
        Codec::writeString(body, m);
    }
    return body;
}

// Build an auth response body: 1-byte completed flag + lp_bytes challenge.
ByteArray buildAuthResponseBody(bool completed, const ByteArray& challenge) {
    ByteArray body;
    body.push_back(completed ? 1 : 0);
    Codec::writeByteArray(body, challenge);
    return body;
}

} // namespace

TEST(AuthCodecTest, MechListRequestBodyIsEmpty) {
    EXPECT_TRUE(encodeAuthMechListRequestBody().empty());
}

TEST(AuthCodecTest, DecodeMechListResponseMultiple) {
    auto body = buildMechListBody({"SCRAM-SHA-512", "SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN"});
    auto mechs = decodeAuthMechListResponse(body);
    ASSERT_EQ(mechs.size(), 4u);
    EXPECT_EQ(mechs[0], "SCRAM-SHA-512");
    EXPECT_EQ(mechs[1], "SCRAM-SHA-256");
    EXPECT_EQ(mechs[2], "SCRAM-SHA-1");
    EXPECT_EQ(mechs[3], "PLAIN");
}

TEST(AuthCodecTest, DecodeMechListResponseEmpty) {
    auto body = buildMechListBody({});
    EXPECT_TRUE(decodeAuthMechListResponse(body).empty());
}

TEST(AuthCodecTest, EncodeAuthRequestRoundTrips) {
    const std::string mech = "SCRAM-SHA-256";
    const ByteArray response = {'n', ',', ',', 'n', '=', 'u', ',', 'r', '=', 'x'};

    ByteArray body = encodeAuthRequestBody(mech, response);

    // Decode back with the primitive codec: lp_string mech + lp_bytes response.
    size_t offset = 0;
    EXPECT_EQ(Codec::readString(body, offset), mech);
    EXPECT_EQ(Codec::readByteArray(body, offset), response);
    EXPECT_EQ(offset, body.size());  // no trailing bytes
}

TEST(AuthCodecTest, EncodeAuthRequestEmptyResponse) {
    ByteArray body = encodeAuthRequestBody("SCRAM-SHA-1", {});
    size_t offset = 0;
    EXPECT_EQ(Codec::readString(body, offset), "SCRAM-SHA-1");
    EXPECT_TRUE(Codec::readByteArray(body, offset).empty());
    EXPECT_EQ(offset, body.size());
}

TEST(AuthCodecTest, DecodeAuthResponseNotCompleted) {
    const ByteArray challenge = {'r', '=', 'a', 'b', ',', 's', '=', 'x', 'y'};
    auto parsed = decodeAuthResponse(buildAuthResponseBody(false, challenge));
    EXPECT_FALSE(parsed.completed);
    EXPECT_EQ(parsed.challenge, challenge);
}

TEST(AuthCodecTest, DecodeAuthResponseCompletedWithVerifier) {
    const ByteArray verifier = {'v', '=', 'z', 'z', 'z'};
    auto parsed = decodeAuthResponse(buildAuthResponseBody(true, verifier));
    EXPECT_TRUE(parsed.completed);
    EXPECT_EQ(parsed.challenge, verifier);
}

TEST(AuthCodecTest, DecodeAuthResponseCompletedEmptyChallenge) {
    auto parsed = decodeAuthResponse(buildAuthResponseBody(true, {}));
    EXPECT_TRUE(parsed.completed);
    EXPECT_TRUE(parsed.challenge.empty());
}
