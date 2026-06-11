#pragma once

#include "Types.h"
#include <cstdint>

namespace hotrod {

/**
 * MurmurHash3 implementation - direct port from Java Hot Rod client.
 *
 * Reference:
 * - org.infinispan.commons.hash.MurmurHash3
 * - Based on Austin Appleby's original C implementation
 * - Using x64 variant (64-bit optimized)
 *
 * Hot Rod uses MurmurHash3_x64_32 with seed=9001 for consistent hashing.
 */
class MurmurHash3 {
public:
    /**
     * Hash a byte array using x64 128-bit variant.
     * Returns both 64-bit halves.
     *
     * @param key Data to hash
     * @param seed Random seed value
     * @return Array of 2 uint64_t values [h1, h2]
     */
    static void hash128(const ByteArray& key, int seed, uint64_t result[2]);

    /**
     * Hash a byte array using x64 64-bit variant.
     * Returns only the first 64-bit half (h1).
     *
     * @param key Data to hash
     * @param seed Random seed value
     * @return 64-bit hash value
     */
    static uint64_t hash64(const ByteArray& key, int seed);

    /**
     * Hash a byte array using x64 32-bit variant.
     * Returns upper 32 bits of the 64-bit hash.
     *
     * This is the function used by Hot Rod for consistent hashing.
     *
     * @param key Data to hash
     * @param seed Random seed value (Hot Rod default: 9001)
     * @return 32-bit hash value
     */
    static int32_t hash32(const ByteArray& key, int seed = 9001);

private:
    struct State {
        uint64_t h1;
        uint64_t h2;
        uint64_t k1;
        uint64_t k2;
        uint64_t c1;
        uint64_t c2;
    };

    static uint64_t getblock(const uint8_t* key, size_t i);
    static void bmix(State& state);
    static uint64_t fmix(uint64_t k);
};

} // namespace hotrod
