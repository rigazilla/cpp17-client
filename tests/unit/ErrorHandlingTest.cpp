#include <gtest/gtest.h>
#include "hotrod/HotRodClientException.h"
#include "hotrod/HeaderCodec.h"
#include "hotrod/Codec.h"
#include <vector>
#include <string>

using namespace hotrod;

/**
 * Step 11a — Error handling unit tests.
 *
 * Covers:
 * - The typed HotRodClientException (pure-data fields).
 * - Futility classification: isTransientStatus / isTransient (D4, §3.3).
 * - Ambiguity: outcomeUncertain (axis 1 helper).
 * - Wire decoding of the ERROR response (opcode 0x50 + length-prefixed message).
 *
 * Reference: docs/ERROR_HANDLING_DESIGN.md §3.1, §3.3.
 */

// --- Status constants -------------------------------------------------------

TEST(ErrorHandlingTest, StatusConstantsMatchProtocol) {
    EXPECT_EQ(0x81, Status::INVALID_MAGIC_OR_MESSAGE_ID);
    EXPECT_EQ(0x82, Status::UNKNOWN_COMMAND);
    EXPECT_EQ(0x83, Status::UNKNOWN_VERSION);
    EXPECT_EQ(0x84, Status::REQUEST_PARSING_ERROR);
    EXPECT_EQ(0x85, Status::SERVER_ERROR);
    EXPECT_EQ(0x86, Status::COMMAND_TIMEOUT);
    EXPECT_EQ(0x87, Status::NODE_SUSPECTED);
    EXPECT_EQ(0x88, Status::ILLEGAL_LIFECYCLE_STATE);
    EXPECT_EQ(0x50, Opcode::ERROR_RESPONSE);
}

// --- Exception shape --------------------------------------------------------

TEST(ErrorHandlingTest, ExceptionCarriesRawFacts) {
    HotRodClientException e("boom", FailurePhase::ServerError, Status::SERVER_ERROR,
                            {{"node-a", 11222}, {"node-b", 11322}}, /*ownersExhausted=*/true);

    EXPECT_STREQ("boom", e.what());
    EXPECT_EQ(FailurePhase::ServerError, e.phase);
    ASSERT_TRUE(e.serverStatus.has_value());
    EXPECT_EQ(Status::SERVER_ERROR, *e.serverStatus);
    ASSERT_EQ(2u, e.triedNodes.size());
    EXPECT_EQ("node-a", e.triedNodes[0].host);
    EXPECT_EQ(11222, e.triedNodes[0].port);
    EXPECT_TRUE(e.ownersExhausted);
}

TEST(ErrorHandlingTest, ExceptionDefaults) {
    HotRodClientException e("x", FailurePhase::BeforeSend);
    EXPECT_FALSE(e.serverStatus.has_value());
    EXPECT_TRUE(e.triedNodes.empty());
    EXPECT_FALSE(e.ownersExhausted);
}

TEST(ErrorHandlingTest, IsCatchableAsStdException) {
    try {
        throw HotRodClientException("x", FailurePhase::AfterSend);
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ("x", e.what());
        return;
    }
    FAIL() << "expected to be caught as std::runtime_error";
}

// --- isTransientStatus (futility of a server ERROR code) --------------------

TEST(ErrorHandlingTest, TransientStatuses) {
    // Node-level + command timeout → retry may succeed elsewhere.
    EXPECT_TRUE(isTransientStatus(Status::COMMAND_TIMEOUT));         // 0x86
    EXPECT_TRUE(isTransientStatus(Status::NODE_SUSPECTED));          // 0x87
    EXPECT_TRUE(isTransientStatus(Status::ILLEGAL_LIFECYCLE_STATE)); // 0x88
}

TEST(ErrorHandlingTest, PermanentStatuses) {
    // Request-level: identical request → identical failure anywhere. Futile.
    EXPECT_FALSE(isTransientStatus(Status::INVALID_MAGIC_OR_MESSAGE_ID)); // 0x81
    EXPECT_FALSE(isTransientStatus(Status::UNKNOWN_COMMAND));             // 0x82
    EXPECT_FALSE(isTransientStatus(Status::UNKNOWN_VERSION));             // 0x83
    EXPECT_FALSE(isTransientStatus(Status::REQUEST_PARSING_ERROR));       // 0x84
    EXPECT_FALSE(isTransientStatus(Status::SERVER_ERROR));                // 0x85
    EXPECT_FALSE(isTransientStatus(0xFF));                                // unknown
}

// --- isTransient (per phase) ------------------------------------------------

TEST(ErrorHandlingTest, IsTransientByPhase) {
    EXPECT_TRUE(isTransient(HotRodClientException("x", FailurePhase::BeforeSend)));
    EXPECT_TRUE(isTransient(HotRodClientException("x", FailurePhase::AfterSend)));

    // ServerError classified by status.
    EXPECT_TRUE(isTransient(HotRodClientException(
        "x", FailurePhase::ServerError, Status::NODE_SUSPECTED)));
    EXPECT_TRUE(isTransient(HotRodClientException(
        "x", FailurePhase::ServerError, Status::COMMAND_TIMEOUT)));
    EXPECT_FALSE(isTransient(HotRodClientException(
        "x", FailurePhase::ServerError, Status::SERVER_ERROR)));

    // ServerError with no status (e.g. opcode mismatch) → not transient.
    EXPECT_FALSE(isTransient(HotRodClientException("x", FailurePhase::ServerError)));
}

// --- outcomeUncertain (ambiguity, axis 1) -----------------------------------

TEST(ErrorHandlingTest, OutcomeUncertain) {
    // AfterSend: sent but no usable reply → may have applied.
    EXPECT_TRUE(outcomeUncertain(HotRodClientException("x", FailurePhase::AfterSend)));
    // Server command timeout: server may have applied before giving up.
    EXPECT_TRUE(outcomeUncertain(HotRodClientException(
        "x", FailurePhase::ServerError, Status::COMMAND_TIMEOUT)));

    // Clean before-send or a definite server rejection: NOT uncertain.
    EXPECT_FALSE(outcomeUncertain(HotRodClientException("x", FailurePhase::BeforeSend)));
    EXPECT_FALSE(outcomeUncertain(HotRodClientException(
        "x", FailurePhase::ServerError, Status::SERVER_ERROR)));
}

TEST(ErrorHandlingTest, TimeoutIsTransientAndUncertain) {
    // 0x86 has the same profile as AfterSend: retry-allowed but ambiguous.
    HotRodClientException e("timeout", FailurePhase::ServerError, Status::COMMAND_TIMEOUT);
    EXPECT_TRUE(isTransient(e));
    EXPECT_TRUE(outcomeUncertain(e));
}

// --- ERROR response wire decoding (opcode 0x50) -----------------------------

TEST(ErrorHandlingTest, ParseErrorResponseBody) {
    // Craft a server ERROR response:
    //   Magic 0xA1 · msgId 1 · opcode 0x50 · status 0x85 · topoMarker 0x00
    //   error message (lp_string): len + UTF-8 bytes
    const std::string msg = "java.lang.NullPointerException";
    ByteArray response = {
        0xA1,        // Magic
        0x01,        // Message ID = 1
        0x50,        // Opcode = ERROR_RESPONSE
        0x85,        // Status = SERVER_ERROR
        0x00         // Topology change = none
    };
    Codec::writeString(response, msg);  // lp_string body

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    EXPECT_EQ(Opcode::ERROR_RESPONSE, header.opcode);
    EXPECT_EQ(Status::SERVER_ERROR, header.status);

    std::string decoded = Codec::readString(response, offset);
    EXPECT_EQ(msg, decoded);
    EXPECT_EQ(response.size(), offset);  // whole body consumed → stream stays in sync
}

TEST(ErrorHandlingTest, ParseErrorResponseEmptyMessage) {
    ByteArray response = {0xA1, 0x02, 0x50, 0x86, 0x00};
    Codec::writeString(response, "");  // empty message

    size_t offset = 0;
    ResponseHeader header = HeaderCodec::readResponseHeader(response, offset);
    EXPECT_EQ(Opcode::ERROR_RESPONSE, header.opcode);
    EXPECT_EQ(Status::COMMAND_TIMEOUT, header.status);

    std::string decoded = Codec::readString(response, offset);
    EXPECT_TRUE(decoded.empty());
    EXPECT_EQ(response.size(), offset);
}
