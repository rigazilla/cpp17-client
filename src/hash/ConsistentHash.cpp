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
    if (numSegments_ == 0 || segmentOwners_.empty()) {
        return 0;
    }

    // Hash the key using MurmurHash3 with seed=9001
    int32_t hash = MurmurHash3::hash32(key, 9001);

    // Normalize hash (mask off sign bit to make positive)
    int32_t normalizedHash = hash & 0x7FFFFFFF;

    // Java uses DIVISION, not modulo:
    // segmentSize = ceil((2^31) / numSegments)
    // segment = normalizedHash / segmentSize
    //
    // This is equivalent to: segment = (normalizedHash * numSegments) / 2^31
    // Using 64-bit to avoid overflow
    int segment = (static_cast<int64_t>(normalizedHash) * numSegments_) / (1L << 31);

    // Extra safety: ensure segment is valid
    if (segment < 0 || segment >= static_cast<int>(segmentOwners_.size())) {
        return 0;
    }

    return segment;
}

const ServerInfo* ConsistentHash::getPrimaryOwner(const ByteArray& key,
                                                  const TopologyInfo& topology) const {
    if (!hasHashTopology()) {
        return nullptr;
    }

    int segment = getSegment(key);

    // Bounds check
    if (segment < 0 || segment >= static_cast<int>(segmentOwners_.size())) {
        fprintf(stderr, "[WARN] ConsistentHash::getPrimaryOwner: segment %d out of bounds (size=%zu)\n",
                segment, segmentOwners_.size());
        return nullptr;
    }

    if (segmentOwners_[segment].empty()) {
        fprintf(stderr, "[WARN] ConsistentHash::getPrimaryOwner: segment %d has no owners\n", segment);
        return nullptr;
    }

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

    // Bounds check
    if (segment < 0 || segment >= static_cast<int>(segmentOwners_.size())) {
        fprintf(stderr, "[WARN] ConsistentHash::getOwners: segment %d out of bounds (size=%zu)\n",
                segment, segmentOwners_.size());
        return owners;
    }

    // Collect all owners for this segment
    for (size_t i = 0; i < segmentOwners_[segment].size(); i++) {
        int32_t hashId = segmentOwners_[segment][i];
        const ServerInfo* server = findServerByHashId(hashId, topology);
        if (server != nullptr) {
            owners.push_back(server);
        }
    }

    return owners;
}

void ConsistentHash::updateFromTopology(const TopologyInfo& topology) {
    if (!topology.hasHashTopology()) {
        clear();
        return;
    }

    const auto& servers = topology.getServers();
    const auto& topoSegmentOwners = topology.getSegmentOwners();

    numSegments_ = static_cast<int>(topology.getNumSegments());

    // Validate: segment owners array must match numSegments
    if (topoSegmentOwners.size() != static_cast<size_t>(numSegments_)) {
        fprintf(stderr, "[WARN] ConsistentHash: topology has %d segments but segmentOwners has %zu entries, clearing hash topology\n",
                numSegments_, topoSegmentOwners.size());
        clear();
        return;
    }

    numOwners_ = topoSegmentOwners.empty() ? 0 : static_cast<int>(topoSegmentOwners[0].size());

    // Convert segment owners from server indices (uint8) to hashIds (int32)
    segmentOwners_.clear();
    segmentOwners_.resize(numSegments_);

    for (int seg = 0; seg < numSegments_; seg++) {
        const auto& ownerIndices = topoSegmentOwners[seg];
        segmentOwners_[seg].resize(ownerIndices.size());

        for (size_t i = 0; i < ownerIndices.size(); i++) {
            uint8_t serverIndex = ownerIndices[i];
            if (serverIndex < servers.size()) {
                segmentOwners_[seg][i] = servers[serverIndex].hashId;
            } else {
                fprintf(stderr, "[WARN] ConsistentHash: invalid server index %d in segment %d (only %zu servers), clearing hash topology\n",
                        serverIndex, seg, servers.size());
                clear();
                return;
            }
        }
    }
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
