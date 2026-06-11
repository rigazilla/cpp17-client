#include "hotrod/Codec.h"
#include <stdexcept>

namespace hotrod {

void Codec::writeVInt(ByteArray& buffer, VInt value) {
    // TODO: Step 1 - Implement vInt encoding
    // Reference: org.infinispan.client.hotrod.impl.transport.netty.ByteBufUtil.writeVInt()
    (void)buffer;
    (void)value;
    throw std::runtime_error("Not implemented: writeVInt");
}

VInt Codec::readVInt(const ByteArray& buffer, size_t& offset) {
    // TODO: Step 1 - Implement vInt decoding
    (void)buffer;
    (void)offset;
    throw std::runtime_error("Not implemented: readVInt");
}

void Codec::writeVLong(ByteArray& buffer, VLong value) {
    // TODO: Step 1 - Implement vLong encoding
    (void)buffer;
    (void)value;
    throw std::runtime_error("Not implemented: writeVLong");
}

VLong Codec::readVLong(const ByteArray& buffer, size_t& offset) {
    // TODO: Step 1 - Implement vLong decoding
    (void)buffer;
    (void)offset;
    throw std::runtime_error("Not implemented: readVLong");
}

void Codec::writeString(ByteArray& buffer, const std::string& value) {
    // TODO: Step 1 - Implement string encoding
    (void)buffer;
    (void)value;
    throw std::runtime_error("Not implemented: writeString");
}

std::string Codec::readString(const ByteArray& buffer, size_t& offset) {
    // TODO: Step 1 - Implement string decoding
    (void)buffer;
    (void)offset;
    throw std::runtime_error("Not implemented: readString");
}

void Codec::writeByteArray(ByteArray& buffer, const ByteArray& value) {
    // TODO: Step 1 - Implement byte array encoding
    (void)buffer;
    (void)value;
    throw std::runtime_error("Not implemented: writeByteArray");
}

ByteArray Codec::readByteArray(const ByteArray& buffer, size_t& offset) {
    // TODO: Step 1 - Implement byte array decoding
    (void)buffer;
    (void)offset;
    throw std::runtime_error("Not implemented: readByteArray");
}

} // namespace hotrod
