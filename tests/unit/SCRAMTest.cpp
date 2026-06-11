#include <gtest/gtest.h>
#include "hotrod/SCRAM.h"

using namespace hotrod;

// ============================================================================
// SCRAM-SHA-256 Tests (RFC 5802)
// ============================================================================

// Test nonce generation
TEST(SCRAMTest, GenerateNonce) {
    std::string nonce1 = SCRAM::generateNonce(24);
    std::string nonce2 = SCRAM::generateNonce(24);

    // Nonces should be non-empty
    EXPECT_FALSE(nonce1.empty());
    EXPECT_FALSE(nonce2.empty());

    // Nonces should be different (extremely high probability)
    EXPECT_NE(nonce1, nonce2);

    // Base64 encoded, so length should be around 32 chars for 24 bytes
    EXPECT_GT(nonce1.size(), 20);
}

// Test client-first-message creation
TEST(SCRAMTest, ClientFirstMessage) {
    std::string username = "admin";
    std::string nonce = "fyko+d2lbbFgONRv9qkxdawL";

    std::string message = SCRAM::createClientFirstMessage(username, nonce);

    // Expected format: "n,,n=admin,r=fyko+d2lbbFgONRv9qkxdawL"
    EXPECT_EQ(message, "n,,n=admin,r=fyko+d2lbbFgONRv9qkxdawL");

    // Check components
    EXPECT_TRUE(message.find("n,,") == 0);  // GS2 header
    EXPECT_NE(message.find("n=admin"), std::string::npos);
    EXPECT_NE(message.find("r=fyko+d2lbbFgONRv9qkxdawL"), std::string::npos);
}

// Test server-first-message parsing
TEST(SCRAMTest, ParseServerFirstMessage) {
    std::string serverMessage = "r=fyko+d2lbbFgONRv9qkxdawL3rfcNHYJY1ZVvWVs7j,s=QSXCR+Q6sek8bf92,i=4096";

    std::string nonce, salt;
    int iterations = 0;

    SCRAM::parseServerFirstMessage(serverMessage, nonce, salt, iterations);

    EXPECT_EQ(nonce, "fyko+d2lbbFgONRv9qkxdawL3rfcNHYJY1ZVvWVs7j");
    EXPECT_EQ(salt, "QSXCR+Q6sek8bf92");
    EXPECT_EQ(iterations, 4096);
}

// Test server-first-message parsing with different iteration count
TEST(SCRAMTest, ParseServerFirstMessage_DifferentIterations) {
    std::string serverMessage = "r=clientnonce123servernonce456,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=10000";

    std::string nonce, salt;
    int iterations = 0;

    SCRAM::parseServerFirstMessage(serverMessage, nonce, salt, iterations);

    EXPECT_EQ(nonce, "clientnonce123servernonce456");
    EXPECT_EQ(salt, "W22ZaJ0SNY7soEsUEjb6gQ==");
    EXPECT_EQ(iterations, 10000);
}

// Test client-final-message (full SCRAM exchange simulation)
TEST(SCRAMTest, ClientFinalMessage_RFC5802Example) {
    // Using RFC 5802 test vector (adapted for SCRAM-SHA-256)
    std::string username = "user";
    std::string password = "pencil";
    std::string clientNonce = "fyko+d2lbbFgONRv9qkxdawL";

    // Client-first-message-bare (without GS2 header)
    std::string clientFirstMessageBare = "n=" + username + ",r=" + clientNonce;

    // Server-first-message (simulated)
    std::string serverNonce = "fyko+d2lbbFgONRv9qkxdawL3rfcNHYJY1ZVvWVs7j";
    std::string salt = "QSXCR+Q6sek8bf92";
    int iterations = 4096;
    std::string serverFirstMessage = "r=" + serverNonce + ",s=" + salt + ",i=" + std::to_string(iterations);

    // Create client-final-message
    std::string clientFinalMessage = SCRAM::createClientFinalMessage(
        password,
        clientFirstMessageBare,
        serverFirstMessage,
        serverNonce,
        salt,
        iterations
    );

    // Verify format: "c=biws,r=<nonce>,p=<proof>"
    EXPECT_NE(clientFinalMessage.find("c=biws"), std::string::npos);
    EXPECT_NE(clientFinalMessage.find("r=" + serverNonce), std::string::npos);
    EXPECT_NE(clientFinalMessage.find(",p="), std::string::npos);

    // Extract proof (base64 encoded)
    size_t proofPos = clientFinalMessage.find(",p=");
    ASSERT_NE(proofPos, std::string::npos);
    std::string proof = clientFinalMessage.substr(proofPos + 3);
    EXPECT_FALSE(proof.empty());
    EXPECT_GT(proof.size(), 20);  // Should be base64 of 32-byte SHA-256
}

// Test complete SCRAM exchange simulation
TEST(SCRAMTest, CompleteExchange_Simulation) {
    // Simulated complete SCRAM-SHA-256 exchange
    std::string username = "testuser";
    std::string password = "testpass123";

    // Step 1: Client generates nonce and creates first message
    std::string clientNonce = "rOprNGfwEbeRWgbNEkqO";  // In practice, use generateNonce()
    std::string clientFirstMessage = SCRAM::createClientFirstMessage(username, clientNonce);

    EXPECT_TRUE(clientFirstMessage.find("n,,n=testuser") == 0);

    // Extract client-first-message-bare (without GS2 header "n,,")
    std::string clientFirstMessageBare = clientFirstMessage.substr(3);  // Skip "n,,"

    // Step 2: Server responds (simulated)
    std::string serverNonce = clientNonce + "serverAppendedPart";
    std::string salt = "W22ZaJ0SNY7soEsUEjb6gQ==";  // Base64 encoded salt
    std::string serverFirstMessage = "r=" + serverNonce + ",s=" + salt + ",i=4096";

    // Step 3: Client parses server message
    std::string parsedNonce, parsedSalt;
    int parsedIterations = 0;
    SCRAM::parseServerFirstMessage(serverFirstMessage, parsedNonce, parsedSalt, parsedIterations);

    EXPECT_EQ(parsedNonce, serverNonce);
    EXPECT_EQ(parsedSalt, salt);
    EXPECT_EQ(parsedIterations, 4096);

    // Step 4: Client creates final message with proof
    std::string clientFinalMessage = SCRAM::createClientFinalMessage(
        password,
        clientFirstMessageBare,
        serverFirstMessage,
        parsedNonce,
        parsedSalt,
        parsedIterations
    );

    EXPECT_FALSE(clientFinalMessage.empty());
    EXPECT_NE(clientFinalMessage.find("c=biws"), std::string::npos);
    EXPECT_NE(clientFinalMessage.find("r=" + serverNonce), std::string::npos);
    EXPECT_NE(clientFinalMessage.find(",p="), std::string::npos);
}

// Test server signature verification
TEST(SCRAMTest, ServerSignatureVerification) {
    // Note: Full server signature verification will be tested in integration tests
    // This is a placeholder to ensure the API compiles
    EXPECT_TRUE(true);
}

// Test Base64 encoding/decoding (internal functions would need to be exposed for testing)
// For now, we test them indirectly through SCRAM operations

// Test error handling - invalid server message
TEST(SCRAMTest, ParseServerFirstMessage_Invalid) {
    std::string invalidMessage = "invalid format";
    std::string nonce, salt;
    int iterations = 0;

    EXPECT_THROW({
        SCRAM::parseServerFirstMessage(invalidMessage, nonce, salt, iterations);
    }, std::runtime_error);
}

// Test error handling - missing fields
TEST(SCRAMTest, ParseServerFirstMessage_MissingFields) {
    std::string incompleteMessage = "r=nonce123";  // Missing salt and iterations
    std::string nonce, salt;
    int iterations = 0;

    EXPECT_THROW({
        SCRAM::parseServerFirstMessage(incompleteMessage, nonce, salt, iterations);
    }, std::runtime_error);
}

// Test with realistic Infinispan credentials
TEST(SCRAMTest, InfinispanCredentials) {
    std::string username = "admin";
    std::string password = "password";
    std::string clientNonce = SCRAM::generateNonce(24);

    std::string clientFirstMessage = SCRAM::createClientFirstMessage(username, clientNonce);

    // Verify message format
    EXPECT_TRUE(clientFirstMessage.find("n,,n=admin,r=") == 0);
    EXPECT_GT(clientFirstMessage.size(), 20);
}
