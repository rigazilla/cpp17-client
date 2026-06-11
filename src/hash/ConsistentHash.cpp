#include "hotrod/ConsistentHash.h"
#include "hotrod/Codec.h"
#include <stdexcept>
#include <algorithm>

namespace hotrod {

ConsistentHash::ConsistentHash()
    : numSegments_(0), numOwners_(0) {
}

void ConsistentHash::parseHashTopology(const ByteArray& buffer, size_t& offset,
                                       const TopologyInfo& /* topology */) {
    // Read number of segments (vInt)
    numSegments_ = static_cast<int>(Codec::readVInt(buffer, offset));

    if (numSegments_ <= 0) {
        throw std::runtime_error("ConsistentHash: invalid number of segments");
    }

    // Read number of owners per segment (uint8, 1 byte)
    if (offset >= buffer.size()) {
        throw std::runtime_error("ConsistentHash: buffer too short for numOwners");
    }
    numOwners_ = static_cast<int>(buffer[offset++]);

    if (numOwners_ <= 0) {
        throw std::runtime_error("ConsistentHash: invalid number of owners");
    }

    // Allocate segment ownership table
    segmentOwners_.clear();
    segmentOwners_.resize(numSegments_);

    // Read ownership for each segment
    for (int seg = 0; seg < numSegments_; seg++) {
        segmentOwners_[seg].resize(numOwners_);

        for (int owner = 0; owner < numOwners_; owner++) {
            // Read server hash ID (int32, 4 bytes big-endian)
            if (offset + 4 > buffer.size()) {
                throw std::runtime_error("ConsistentHash: buffer too short for hash ID");
            }

            int32_t hashId = static_cast<int32_t>(
                (buffer[offset] << 24) |
                (buffer[offset + 1] << 16) |
                (buffer[offset + 2] << 8) |
                buffer[offset + 3]
            );
            offset += 4;

            segmentOwners_[seg][owner] = hashId;
        }
    }
}

int ConsistentHash::getSegment(const ByteArray& key) const {
    if (numSegments_ == 0) {
        return 0;
    }

    // Hash the key using MurmurHash3 with seed=9001
    int32_t hash = MurmurHash3::hash32(key, 9001);

    // Mask off sign bit and modulo to get segment
    // Java: (hash & 0x7FFFFFFF) % numSegments
    int segment = (hash & 0x7FFFFFFF) % numSegments_;

    return segment;
}

const ServerInfo* ConsistentHash::getPrimaryOwner(const ByteArray& key,
                                                  const TopologyInfo& topology) const {
    if (!hasHashTopology()) {
        return nullptr;
    }

    int segment = getSegment(key);

    // Primary owner is the first in the list
    int32_t primaryHashId = segmentOwners_[segment][0];

    return findServerByHashId(primaryHashId, topology);
}

std::vector<const ServerInfo*> ConsistentHash::getOwners(const ByteArray& key,
                                                         const TopologyInfo& topology) const {
    std::vector<const ServerInfo*> owners;

    if (!hasHashTopology()) {
        return owners;
    }

    int segment = getSegment(key);

    // Collect all owners for this segment
    for (int i = 0; i < numOwners_; i++) {
        int32_t hashId = segmentOwners_[segment][i];
        const ServerInfo* server = findServerByHashId(hashId, topology);
        if (server != nullptr) {
            owners.push_back(server);
        }
    }

    return owners;
}

void ConsistentHash::clear() {
    numSegments_ = 0;
    numOwners_ = 0;
    segmentOwners_.clear();
}

const ServerInfo* ConsistentHash::findServerByHashId(int32_t hashId,
                                                     const TopologyInfo& topology) const {
    const auto& servers = topology.getServers();

    for (const auto& server : servers) {
        if (server.hashId == hashId) {
            return &server;
        }
    }

    return nullptr;
}

} // namespace hotrod
