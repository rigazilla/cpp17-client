#pragma once

#include "Types.h"
#include "Codec.h"
#include <string>
#include <vector>

namespace hotrod {

/**
 * Encode/decode the *bodies* of the SASL authentication messages (the request
 * header is built separately by HeaderCodec). Pure, I/O-free helpers so they can
 * be unit-tested directly — same header-only inline precedent as ServerSelection.h.
 *
 * Wire layout (authoritative: Kaitai hotrod40.ksy; all length prefixes are vint):
 *  - AUTH_MECH_LIST_REQUEST (0x21): header only, empty body.
 *  - AUTH_MECH_LIST_RESPONSE (0x22): vint mech_count, then mech_count x lp_string.
 *  - AUTH_REQUEST (0x23): lp_string mech + lp_bytes response_data.
 *  - AUTH_RESPONSE (0x24): u1 completed (0 = false) + lp_bytes challenge_data.
 *
 * Reference:
 * - Java: AuthMechListOperation / AuthOperation (writeOperationRequest / createResponse)
 */

/**
 * Result of decoding an AUTH_RESPONSE (0x24) body.
 */
struct AuthResponseBody {
    bool      completed = false;  // SASL exchange complete (server set the flag)
    ByteArray challenge;          // server challenge / final SASL bytes (may be empty)
};

/**
 * AUTH_MECH_LIST_REQUEST (0x21) body — empty by protocol.
 */
inline ByteArray encodeAuthMechListRequestBody() {
    return {};
}

/**
 * Decode an AUTH_MECH_LIST_RESPONSE (0x22) body into the offered mechanism names.
 */
inline std::vector<std::string> decodeAuthMechListResponse(const ByteArray& body) {
    size_t offset = 0;
    VInt count = Codec::readVInt(body, offset);
    std::vector<std::string> mechs;
    mechs.reserve(count);
    for (VInt i = 0; i < count; ++i) {
        mechs.push_back(Codec::readString(body, offset));
    }
    return mechs;
}

/**
 * Encode an AUTH_REQUEST (0x23) body: mechanism name + SASL response token.
 * The mechanism name is (re-)sent on every round of the exchange.
 */
inline ByteArray encodeAuthRequestBody(const std::string& mechanism,
                                       const ByteArray& saslResponse) {
    ByteArray body;
    Codec::writeString(body, mechanism);
    Codec::writeByteArray(body, saslResponse);
    return body;
}

/**
 * Decode an AUTH_RESPONSE (0x24) body: completed flag + challenge token.
 */
inline AuthResponseBody decodeAuthResponse(const ByteArray& body) {
    AuthResponseBody out;
    // completed is a plain 1-byte u1 flag (not a vint), followed by lp_bytes.
    size_t offset = 0;
    out.completed = body.at(offset) != 0;
    ++offset;
    out.challenge = Codec::readByteArray(body, offset);
    return out;
}

} // namespace hotrod
