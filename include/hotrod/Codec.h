#pragma once

#include "Types.h"

namespace hotrod {

/**
 * Wire format encoding/decoding utilities for Hot Rod protocol primitives.
 *
 * Reference: hotrod-foundry/docs/01-wire-format-primitives.md
 * Java: org.infinispan.client.hotrod.impl.transport.netty.ByteBufUtil
 */
class Codec {
public:
    // Variable-length integer encoding (vInt - 32-bit unsigned)
    static void writeVInt(ByteArray& buffer, VInt value);
    static VInt readVInt(const ByteArray& buffer, size_t& offset);

    // Variable-length long encoding (vLong - 64-bit unsigned)
    static void writeVLong(ByteArray& buffer, VLong value);
    static VLong readVLong(const ByteArray& buffer, size_t& offset);

    // String encoding (vInt length + UTF-8 bytes)
    static void writeString(ByteArray& buffer, const std::string& value);
    static std::string readString(const ByteArray& buffer, size_t& offset);

    // Byte array encoding (vInt length + raw bytes)
    static void writeByteArray(ByteArray& buffer, const ByteArray& value);
    static ByteArray readByteArray(const ByteArray& buffer, size_t& offset);
};

} // namespace hotrod
