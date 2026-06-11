#include "hotrod/HeaderCodec.h"
#include "hotrod/Codec.h"
#include <stdexcept>

namespace hotrod {

// Request header encoding (Protocol 4.0 complete spec)
// Reference: Codec40.writeHeader() + Codec30.writeHeader() + hotrod40.ksy lines 315-354
void HeaderCodec::writeRequestHeader(ByteArray& buffer, const RequestHeader& header) {
    // 1. Magic byte (0xA0)
    buffer.push_back(header.magic);

    // 2. Message ID (vLong)
    Codec::writeVLong(buffer, header.messageId);

    // 3. Version (1 byte)
    buffer.push_back(header.version);

    // 4. Opcode (1 byte)
    buffer.push_back(header.opcode);

    // 5. Cache name (string = vInt length + UTF-8 bytes)
    Codec::writeString(buffer, header.cacheName);

    // 6. Flags (vInt)
    Codec::writeVInt(buffer, header.flags);

    // 7. Client intelligence (1 byte)
    buffer.push_back(static_cast<uint8_t>(header.clientIntelligence));

    // 8. Topology ID (vInt)
    Codec::writeVInt(buffer, header.topologyId);

    // 9. Key media type (if version >= 0x28) - Protocol 2.8+
    // NOTE: This is REQUIRED for Protocol 4.0 (version 0x28 = 40 decimal)
    if (header.version >= 0x28) {
        buffer.push_back(header.keyMediaType);
    }

    // 10. Value media type (if version >= 0x28) - Protocol 2.8+
    if (header.version >= 0x28) {
        buffer.push_back(header.valueMediaType);
    }

    // 11. Other param count (if version >= 40) - Protocol 4.0+
    // NOTE: This is REQUIRED for Protocol 4.0 (usually 0)
    if (header.version >= 40) {
        Codec::writeVInt(buffer, static_cast<VInt>(header.otherParams.size()));

        // 12. Other params (key-value pairs, if count > 0)
        for (const auto& [key, value] : header.otherParams) {
            Codec::writeString(buffer, key);
            Codec::writeByteArray(buffer, value);
        }
    }
}

// Response header decoding
// Reference: Codec30.java response reading, hotrod40.ksy lines 360+
ResponseHeader HeaderCodec::readResponseHeader(const ByteArray& buffer, size_t& offset) {
    ResponseHeader header;

    // 1. Magic byte (0xA1)
    if (offset >= buffer.size()) {
        throw std::runtime_error("readResponseHeader: buffer too short for magic byte");
    }
    header.magic = buffer[offset++];

    if (header.magic != Protocol::RESPONSE_MAGIC) {
        throw std::runtime_error("readResponseHeader: invalid magic byte (expected 0xA1, got 0x" +
                                 std::to_string(header.magic) + ")");
    }

    // 2. Message ID (vLong)
    header.messageId = Codec::readVLong(buffer, offset);

    // 3. Opcode (1 byte)
    if (offset >= buffer.size()) {
        throw std::runtime_error("readResponseHeader: buffer too short for opcode");
    }
    header.opcode = buffer[offset++];

    // 4. Status (1 byte)
    if (offset >= buffer.size()) {
        throw std::runtime_error("readResponseHeader: buffer too short for status");
    }
    header.status = buffer[offset++];

    // 5. Topology change marker (1 byte)
    if (offset >= buffer.size()) {
        throw std::runtime_error("readResponseHeader: buffer too short for topology marker");
    }
    header.topologyChangeMarker = buffer[offset++];

    // Note: Topology data parsing will be handled in Step 4 (Topology Awareness)
    // For now, we just read the marker and ignore topology data if present

    return header;
}

} // namespace hotrod
