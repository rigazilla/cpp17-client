#pragma once

#include "Types.h"
#include "TopologyInfo.h"
#include "MurmurHash3.h"
#include <vector>
#include <cstdint>

namespace hotrod {

/**
 * Consistent hashing for smart key routing.
 *
 * With client intelligence = 0x03 (HASH_DISTRIBUTION_AWARE), the client can
 * route each key directly to its primary owner using MurmurHash3 and segment ownership.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.consistenthash.ConsistentHashV2
 * - ROADMAP Step 5
 */
class ConsistentHash {
public:
    ConsistentHash();

    /**
     * Parse hash topology from response.
     *
     * Wire format (when topology_change_marker = 0x01 and client_intelligence = 0x03):
     * After standard topology info (from TopologyInfo):
     * - Number of segments (vInt) - usually 256
     * - Number of owners per segment (uint8)
     * - For each segment:
     *   - For each owner:
     *     - Server hash ID (int32, references ServerInfo.hashId)
     *
     * @param buffer Response buffer
     * @param offset Current read position (updated)
     * @param topology Topology info with server list
     */
    void parseHashTopology(const ByteArray& buffer, size_t& offset,
                          const TopologyInfo& topology);

    /**
     * Get the segment number for a key.
     *
     * Algorithm:
     * 1. hash = MurmurHash3.hash32(key, seed=9001)
     * 2. segment = (hash & 0x7FFFFFFF) % numSegments
     *
     * @param key The key to hash
     * @return Segment number (0 to numSegments-1)
     */
    int getSegment(const ByteArray& key) const;

    /**
     * Get the primary owner server for a key.
     *
     * @param key The key to route
     * @param topology Topology info with server list
     * @return Pointer to primary owner ServerInfo, or nullptr if no hash topology
     */
    const ServerInfo* getPrimaryOwner(const ByteArray& key,
                                      const TopologyInfo& topology) const;

    /**
     * Get all owner servers for a key (primary + backups).
     *
     * @param key The key
     * @param topology Topology info with server list
     * @return Vector of owner ServerInfo pointers (empty if no hash topology)
     */
    std::vector<const ServerInfo*> getOwners(const ByteArray& key,
                                             const TopologyInfo& topology) const;

    /**
     * Check if hash topology is available.
     */
    bool hasHashTopology() const { return numSegments_ > 0; }

    /**
     * Get number of segments.
     */
    int getNumSegments() const { return numSegments_; }

    /**
     * Get number of owners per segment.
     */
    int getNumOwners() const { return numOwners_; }

    /**
     * Clear hash topology.
     */
    void clear();

private:
    int numSegments_;  // Usually 256
    int numOwners_;    // Number of owners per segment (typically 2)

    // segmentOwners_[segment][ownerIndex] = server hash ID
    std::vector<std::vector<int32_t>> segmentOwners_;

    // Find server by hash ID in topology
    const ServerInfo* findServerByHashId(int32_t hashId,
                                        const TopologyInfo& topology) const;
};

} // namespace hotrod
