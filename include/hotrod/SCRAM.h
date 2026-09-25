#pragma once

#include "Types.h"
#include <string>

namespace hotrod {

/**
 * SCRAM-SHA-256 SASL authentication mechanism (RFC 5802).
 *
 * Reference:
 * - RFC 5802: https://tools.ietf.org/html/rfc5802
 * - Java: javax.security.sasl (SaslClient with SCRAM-SHA-256)
 */
class SCRAM {
public:
    /**
     * SCRAM digest family. Selects the underlying hash for HMAC / PBKDF2 / H,
     * and therefore the SaltedPassword / key / signature lengths:
     * SHA-1 = 20, SHA-256 = 32, SHA-512 = 64 bytes.
     */
    enum class Digest { SHA1, SHA256, SHA512 };

    /**
     * Map a SASL mechanism name to its digest.
     * @throws std::runtime_error if the mechanism is not a supported SCRAM variant
     */
    static Digest digestForMechanism(const std::string& mechanism);

    /**
     * Generate a cryptographically secure random nonce.
     * @param length Number of bytes for the nonce
     * @return Base64-encoded nonce
     */
    static std::string generateNonce(size_t length = 24);

    /**
     * Create the client-first-message.
     * Format: "n,,n=<username>,r=<nonce>"
     *
     * @param username The username
     * @param nonce The client nonce
     * @return Client-first-message
     */
    static std::string createClientFirstMessage(const std::string& username, const std::string& nonce);

    /**
     * Parse the server-first-message.
     * Format: "r=<combined-nonce>,s=<salt>,i=<iterations>"
     *
     * @param message Server-first-message
     * @param outNonce Combined nonce (client + server)
     * @param outSalt Base64-encoded salt
     * @param outIterations PBKDF2 iteration count
     */
    static void parseServerFirstMessage(const std::string& message,
                                       std::string& outNonce,
                                       std::string& outSalt,
                                       int& outIterations);

    /**
     * Create the client-final-message.
     * Format: "c=<channel-binding>,r=<nonce>,p=<proof>"
     *
     * @param password The user's password
     * @param clientFirstMessageBare Client-first without GS2 header (e.g., "n=user,r=nonce")
     * @param serverFirstMessage The server's first message
     * @param nonce Combined nonce from server
     * @param salt Base64-encoded salt
     * @param iterations PBKDF2 iteration count
     * @param digest SCRAM digest family (defaults to SHA-256)
     * @return Client-final-message
     */
    static std::string createClientFinalMessage(const std::string& password,
                                                const std::string& clientFirstMessageBare,
                                                const std::string& serverFirstMessage,
                                                const std::string& nonce,
                                                const std::string& salt,
                                                int iterations,
                                                Digest digest = Digest::SHA256);

    /**
     * Verify the server-final-message.
     * Format: "v=<server-signature>"
     *
     * @param message Server-final-message
     * @param password The user's password
     * @param authMessage The authentication message (client-first-bare + "," + server-first + "," + client-final-without-proof)
     * @param salt Base64-encoded salt
     * @param iterations PBKDF2 iteration count
     * @param digest SCRAM digest family (defaults to SHA-256)
     * @return true if server signature is valid
     */
    static bool verifyServerFinalMessage(const std::string& message,
                                        const std::string& password,
                                        const std::string& authMessage,
                                        const std::string& salt,
                                        int iterations,
                                        Digest digest = Digest::SHA256);

private:
    // PBKDF2-HMAC key derivation; output length follows the chosen digest.
    static ByteArray pbkdf2(const std::string& password, const ByteArray& salt, int iterations, Digest digest);

    // HMAC keyed by the chosen digest.
    static ByteArray hmac(const ByteArray& key, const std::string& message, Digest digest);
    static ByteArray hmac(const ByteArray& key, const ByteArray& message, Digest digest);

    // H (plain hash) with the chosen digest — used for StoredKey.
    static ByteArray hash(const ByteArray& data, Digest digest);

    // Base64 encoding/decoding
    static std::string base64Encode(const ByteArray& data);
    static ByteArray base64Decode(const std::string& encoded);

    // XOR two byte arrays
    static ByteArray xorBytes(const ByteArray& a, const ByteArray& b);

    // SASLprep (simplified - just checks for forbidden characters)
    static std::string saslPrep(const std::string& input);
};

} // namespace hotrod
