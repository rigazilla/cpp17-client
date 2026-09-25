#include "hotrod/SCRAM.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace hotrod {

namespace {
// Map a SCRAM digest to its OpenSSL message digest.
const EVP_MD* mdFor(SCRAM::Digest digest) {
    switch (digest) {
        case SCRAM::Digest::SHA1:   return EVP_sha1();
        case SCRAM::Digest::SHA256: return EVP_sha256();
        case SCRAM::Digest::SHA512: return EVP_sha512();
    }
    return EVP_sha256();  // unreachable; keeps the compiler happy
}
} // namespace

// Map a SASL mechanism name to its SCRAM digest.
SCRAM::Digest SCRAM::digestForMechanism(const std::string& mechanism) {
    if (mechanism == "SCRAM-SHA-1")   return Digest::SHA1;
    if (mechanism == "SCRAM-SHA-256") return Digest::SHA256;
    if (mechanism == "SCRAM-SHA-512") return Digest::SHA512;
    throw std::runtime_error("Unsupported SASL mechanism: " + mechanism);
}

// Generate cryptographically secure random nonce
std::string SCRAM::generateNonce(size_t length) {
    ByteArray randomBytes(length);
    if (RAND_bytes(randomBytes.data(), static_cast<int>(length)) != 1) {
        throw std::runtime_error("SCRAM::generateNonce: Failed to generate random bytes");
    }
    return base64Encode(randomBytes);
}

// Create client-first-message: "n,,n=<username>,r=<nonce>"
std::string SCRAM::createClientFirstMessage(const std::string& username, const std::string& nonce) {
    // GS2 header: "n,," (no channel binding)
    // client-first-message-bare: "n=<username>,r=<nonce>"
    return "n,,n=" + username + ",r=" + nonce;
}

// Parse server-first-message: "r=<nonce>,s=<salt>,i=<iterations>"
void SCRAM::parseServerFirstMessage(const std::string& message,
                                    std::string& outNonce,
                                    std::string& outSalt,
                                    int& outIterations) {
    std::istringstream stream(message);
    std::string token;

    while (std::getline(stream, token, ',')) {
        if (token.size() < 2 || token[1] != '=') {
            continue;
        }

        char key = token[0];
        std::string value = token.substr(2);

        switch (key) {
            case 'r':
                outNonce = value;
                break;
            case 's':
                outSalt = value;
                break;
            case 'i':
                outIterations = std::stoi(value);
                break;
        }
    }

    if (outNonce.empty() || outSalt.empty() || outIterations == 0) {
        throw std::runtime_error("SCRAM::parseServerFirstMessage: Invalid server message");
    }
}

// Create client-final-message with proof
std::string SCRAM::createClientFinalMessage(const std::string& password,
                                            const std::string& clientFirstMessageBare,
                                            const std::string& serverFirstMessage,
                                            const std::string& nonce,
                                            const std::string& salt,
                                            int iterations,
                                            Digest digest) {
    // Channel binding: "c=biws" (base64 of "n,,")
    std::string channelBinding = "biws";

    // client-final-without-proof: "c=<channel-binding>,r=<nonce>"
    std::string clientFinalWithoutProof = "c=" + channelBinding + ",r=" + nonce;

    // AuthMessage = client-first-message-bare + "," + server-first-message + "," + client-final-without-proof
    std::string authMessage = clientFirstMessageBare + "," + serverFirstMessage + "," + clientFinalWithoutProof;

    // SaltedPassword = PBKDF2(password, salt, iterations); length follows the digest
    ByteArray saltBytes = base64Decode(salt);
    ByteArray saltedPassword = pbkdf2(password, saltBytes, iterations, digest);

    // ClientKey = HMAC(SaltedPassword, "Client Key")
    ByteArray clientKey = hmac(saltedPassword, "Client Key", digest);

    // StoredKey = H(ClientKey)
    ByteArray storedKey = hash(clientKey, digest);

    // ClientSignature = HMAC(StoredKey, AuthMessage)
    ByteArray clientSignature = hmac(storedKey, authMessage, digest);

    // ClientProof = ClientKey XOR ClientSignature
    ByteArray clientProof = xorBytes(clientKey, clientSignature);

    // client-final-message = client-final-without-proof + ",p=" + base64(ClientProof)
    return clientFinalWithoutProof + ",p=" + base64Encode(clientProof);
}

// Verify server-final-message: "v=<server-signature>"
bool SCRAM::verifyServerFinalMessage(const std::string& message,
                                     const std::string& password,
                                     const std::string& authMessage,
                                     const std::string& salt,
                                     int iterations,
                                     Digest digest) {
    // Extract server signature from message
    if (message.size() < 2 || message[0] != 'v' || message[1] != '=') {
        return false;
    }
    std::string serverSignatureB64 = message.substr(2);

    // Calculate expected server signature
    ByteArray saltBytes = base64Decode(salt);
    ByteArray saltedPassword = pbkdf2(password, saltBytes, iterations, digest);

    // ServerKey = HMAC(SaltedPassword, "Server Key")
    ByteArray serverKey = hmac(saltedPassword, "Server Key", digest);

    // ServerSignature = HMAC(ServerKey, AuthMessage)
    ByteArray expectedSignature = hmac(serverKey, authMessage, digest);

    // Compare with received signature
    ByteArray receivedSignature = base64Decode(serverSignatureB64);
    return expectedSignature == receivedSignature;
}

// PBKDF2-HMAC; derived-key length follows the digest (SHA-1=20, SHA-256=32, SHA-512=64).
ByteArray SCRAM::pbkdf2(const std::string& password, const ByteArray& salt, int iterations, Digest digest) {
    const EVP_MD* md = mdFor(digest);
    const size_t keyLength = static_cast<size_t>(EVP_MD_size(md));
    ByteArray key(keyLength);

    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()),
                          iterations,
                          md,
                          static_cast<int>(keyLength),
                          key.data()) != 1) {
        throw std::runtime_error("SCRAM::pbkdf2: PBKDF2 derivation failed");
    }

    return key;
}

// HMAC (string message)
ByteArray SCRAM::hmac(const ByteArray& key, const std::string& message, Digest digest) {
    return hmac(key, ByteArray(message.begin(), message.end()), digest);
}

// HMAC (byte array message)
ByteArray SCRAM::hmac(const ByteArray& key, const ByteArray& message, Digest digest) {
    const EVP_MD* md = mdFor(digest);
    ByteArray result(EVP_MD_size(md));
    unsigned int resultLen = 0;

    if (HMAC(md,
             key.data(), static_cast<int>(key.size()),
             message.data(), message.size(),
             result.data(), &resultLen) == nullptr) {
        throw std::runtime_error("SCRAM::hmac: HMAC computation failed");
    }

    result.resize(resultLen);
    return result;
}

// H (plain hash) with the chosen digest.
ByteArray SCRAM::hash(const ByteArray& data, Digest digest) {
    const EVP_MD* md = mdFor(digest);
    ByteArray result(EVP_MD_size(md));
    unsigned int resultLen = 0;

    if (EVP_Digest(data.data(), data.size(), result.data(), &resultLen, md, nullptr) != 1) {
        throw std::runtime_error("SCRAM::hash: digest computation failed");
    }

    result.resize(resultLen);
    return result;
}

// Base64 encoding
std::string SCRAM::base64Encode(const ByteArray& data) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);  // No newlines
    BIO_write(bio, data.data(), static_cast<int>(data.size()));
    BIO_flush(bio);

    BUF_MEM* bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);

    return result;
}

// Base64 decoding
ByteArray SCRAM::base64Decode(const std::string& encoded) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    ByteArray result(encoded.size());  // Decoded will be smaller
    int decodedLength = BIO_read(bio, result.data(), static_cast<int>(result.size()));

    BIO_free_all(bio);

    if (decodedLength < 0) {
        throw std::runtime_error("SCRAM::base64Decode: Decoding failed");
    }

    result.resize(decodedLength);
    return result;
}

// XOR two byte arrays
ByteArray SCRAM::xorBytes(const ByteArray& a, const ByteArray& b) {
    if (a.size() != b.size()) {
        throw std::runtime_error("SCRAM::xorBytes: Arrays must be same size");
    }

    ByteArray result(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        result[i] = a[i] ^ b[i];
    }

    return result;
}

// SASLprep (simplified - RFC 4013)
std::string SCRAM::saslPrep(const std::string& input) {
    // Simplified implementation: just check for forbidden characters
    // Full implementation would require Unicode normalization
    for (char c : input) {
        if (c == '\0') {
            throw std::runtime_error("SCRAM::saslPrep: Null character not allowed");
        }
    }
    return input;
}

} // namespace hotrod
