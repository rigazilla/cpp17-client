#include "hotrod/Codec.h"
#include <stdexcept>

namespace hotrod {

// Variable-length integer encoding (vInt - 32-bit unsigned)
// Reference: ByteBufUtil.writeVInt() lines 70-76
void Codec::writeVInt(ByteArray& buffer, VInt value) {
    while ((value & ~0x7F) != 0) {
        buffer.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}

// Variable-length integer decoding (vInt - 32-bit unsigned)
// Reference: ByteBufUtil.readVInt() lines 104-112
VInt Codec::readVInt(const ByteArray& buffer, size_t& offset) {
    if (offset >= buffer.size()) {
        throw std::runtime_error("readVInt: offset out of bounds");
    }

    uint8_t b = buffer[offset++];
    VInt result = b & 0x7F;

    for (int shift = 7; (b & 0x80) != 0; shift += 7) {
        if (offset >= buffer.size()) {
            throw std::runtime_error("readVInt: incomplete varint");
        }
        b = buffer[offset++];
        result |= static_cast<VInt>(b & 0x7F) << shift;
    }

    return result;
}

// Variable-length long encoding (vLong - 64-bit unsigned)
// Reference: ByteBufUtil.writeVLong() lines 82-88
void Codec::writeVLong(ByteArray& buffer, VLong value) {
    while ((value & ~0x7F) != 0) {
        buffer.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}

// Variable-length long decoding (vLong - 64-bit unsigned)
// Reference: ByteBufUtil.readVLong() lines 94-102
VLong Codec::readVLong(const ByteArray& buffer, size_t& offset) {
    if (offset >= buffer.size()) {
        throw std::runtime_error("readVLong: offset out of bounds");
    }

    uint8_t b = buffer[offset++];
    VLong result = b & 0x7F;

    for (int shift = 7; (b & 0x80) != 0; shift += 7) {
        if (offset >= buffer.size()) {
            throw std::runtime_error("readVLong: incomplete varint");
        }
        b = buffer[offset++];
        result |= static_cast<VLong>(b & 0x7F) << shift;
    }

    return result;
}

// String encoding (vInt length + UTF-8 bytes)
// Reference: ByteBufUtil.writeString() lines 31-37
void Codec::writeString(ByteArray& buffer, const std::string& value) {
    if (value.empty()) {
        writeVInt(buffer, 0);
    } else {
        // String is already UTF-8 in C++
        writeVInt(buffer, static_cast<VInt>(value.size()));
        buffer.insert(buffer.end(), value.begin(), value.end());
    }
}

// String decoding (vInt length + UTF-8 bytes)
// Reference: ByteBufUtil.readString() lines 26-29
std::string Codec::readString(const ByteArray& buffer, size_t& offset) {
    VInt length = readVInt(buffer, offset);

    if (length == 0) {
        return "";
    }

    if (offset + length > buffer.size()) {
        throw std::runtime_error("readString: string length exceeds buffer");
    }

    std::string result(buffer.begin() + offset, buffer.begin() + offset + length);
    offset += length;

    return result;
}

// Byte array encoding (vInt length + raw bytes)
// Reference: ByteBufUtil.writeArray() lines 47-50
void Codec::writeByteArray(ByteArray& buffer, const ByteArray& value) {
    writeVInt(buffer, static_cast<VInt>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

// Byte array decoding (vInt length + raw bytes)
// Reference: ByteBufUtil.readArray() lines 19-24
ByteArray Codec::readByteArray(const ByteArray& buffer, size_t& offset) {
    VInt length = readVInt(buffer, offset);

    if (length == 0) {
        return ByteArray();
    }

    if (offset + length > buffer.size()) {
        throw std::runtime_error("readByteArray: array length exceeds buffer");
    }

    ByteArray result(buffer.begin() + offset, buffer.begin() + offset + length);
    offset += length;

    return result;
}

} // namespace hotrod
