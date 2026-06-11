#include "hotrod/MurmurHash3.h"

namespace hotrod {

// Extract 8 bytes from key at position i (little-endian)
uint64_t MurmurHash3::getblock(const uint8_t* key, size_t i) {
    return
          static_cast<uint64_t>(key[i + 0])
        | (static_cast<uint64_t>(key[i + 1]) << 8)
        | (static_cast<uint64_t>(key[i + 2]) << 16)
        | (static_cast<uint64_t>(key[i + 3]) << 24)
        | (static_cast<uint64_t>(key[i + 4]) << 32)
        | (static_cast<uint64_t>(key[i + 5]) << 40)
        | (static_cast<uint64_t>(key[i + 6]) << 48)
        | (static_cast<uint64_t>(key[i + 7]) << 56);
}

// Mix function (same as Java bmix)
void MurmurHash3::bmix(State& state) {
    state.k1 *= state.c1;
    state.k1 = (state.k1 << 23) | (state.k1 >> (64 - 23));
    state.k1 *= state.c2;
    state.h1 ^= state.k1;
    state.h1 += state.h2;

    state.h2 = (state.h2 << 41) | (state.h2 >> (64 - 41));

    state.k2 *= state.c2;
    state.k2 = (state.k2 << 23) | (state.k2 >> (64 - 23));
    state.k2 *= state.c1;
    state.h2 ^= state.k2;
    state.h2 += state.h1;

    state.h1 = state.h1 * 3 + 0x52dce729;
    state.h2 = state.h2 * 3 + 0x38495ab5;

    state.c1 = state.c1 * 5 + 0x7b7d159c;
    state.c2 = state.c2 * 5 + 0x6bce6396;
}

// Finalization mix (same as Java fmix)
uint64_t MurmurHash3::fmix(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;

    return k;
}

// 128-bit hash (returns both h1 and h2)
void MurmurHash3::hash128(const ByteArray& key, int seed, uint64_t result[2]) {
    State state;

    state.h1 = 0x9368e53c2f6af274ULL ^ seed;
    state.h2 = 0x586dcd208f7cd3fdULL ^ seed;

    state.c1 = 0x87c37b91114253d5ULL;
    state.c2 = 0x4cf5ad432745937fULL;

    // Process 16-byte blocks
    for (size_t i = 0; i < key.size() / 16; i++) {
        state.k1 = getblock(key.data(), i * 2 * 8);
        state.k2 = getblock(key.data(), (i * 2 + 1) * 8);

        bmix(state);
    }

    state.k1 = 0;
    state.k2 = 0;

    size_t tail = (key.size() >> 4) << 4;

    // Process remaining bytes (tail)
    // NOTE: Java casts byte (signed) to long, causing sign extension.
    // We mimic this by casting uint8_t to int8_t first, then to int64_t.
    switch (key.size() & 15) {
        case 15: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 14])) << 48; [[fallthrough]];
        case 14: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 13])) << 40; [[fallthrough]];
        case 13: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 12])) << 32; [[fallthrough]];
        case 12: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 11])) << 24; [[fallthrough]];
        case 11: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 10])) << 16; [[fallthrough]];
        case 10: state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 9])) << 8; [[fallthrough]];
        case 9:  state.k2 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 8])); [[fallthrough]];

        case 8:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 7])) << 56; [[fallthrough]];
        case 7:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 6])) << 48; [[fallthrough]];
        case 6:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 5])) << 40; [[fallthrough]];
        case 5:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 4])) << 32; [[fallthrough]];
        case 4:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 3])) << 24; [[fallthrough]];
        case 3:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 2])) << 16; [[fallthrough]];
        case 2:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 1])) << 8; [[fallthrough]];
        case 1:  state.k1 ^= static_cast<int64_t>(static_cast<int8_t>(key[tail + 0]));
            bmix(state);
    }

    state.h2 ^= key.size();

    state.h1 += state.h2;
    state.h2 += state.h1;

    state.h1 = fmix(state.h1);
    state.h2 = fmix(state.h2);

    state.h1 += state.h2;
    state.h2 += state.h1;

    result[0] = state.h1;
    result[1] = state.h2;
}

// 64-bit hash (returns only h1)
uint64_t MurmurHash3::hash64(const ByteArray& key, int seed) {
    uint64_t result[2];
    hash128(key, seed, result);
    return result[0];
}

// 32-bit hash (upper 32 bits of h1) - USED BY HOT ROD
int32_t MurmurHash3::hash32(const ByteArray& key, int seed) {
    uint64_t h64 = hash64(key, seed);
    // Java: (int) (hash64 >>> 32)
    // This extracts the upper 32 bits and casts to signed int32
    return static_cast<int32_t>(h64 >> 32);
}

} // namespace hotrod
