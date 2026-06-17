#include "hotrod/TopologyInfo.h"
#include "hotrod/Codec.h"
#include "hotrod/Connection.h"
#include "hotrod/HeaderCodec.h"
#include <stdexcept>
#include <algorithm>

namespace hotrod {

TopologyInfo::TopologyInfo()
    : topologyId_(0), roundRobinIndex_(0) {
}

const ServerInfo* TopologyInfo::selectServer() {
    if (servers_.empty()) {
        return nullptr;
    }

    // Round-robin selection
    const ServerInfo* server = &servers_[roundRobinIndex_];
    roundRobinIndex_ = (roundRobinIndex_ + 1) % servers_.size();

    return server;
}

void TopologyInfo::addServer(const ServerInfo& server) {
    // Check for duplicates
    if (std::find(servers_.begin(), servers_.end(), server) == servers_.end()) {
        servers_.push_back(server);
    }
}

void TopologyInfo::removeServer(const std::string& host, uint16_t port) {
    servers_.erase(
        std::remove_if(servers_.begin(), servers_.end(),
            [&](const ServerInfo& s) {
                return s.host == host && s.port == port;
            }),
        servers_.end()
    );
}

void TopologyInfo::clearServers() {
    servers_.clear();
    roundRobinIndex_ = 0;
}

void TopologyInfo::parseTopologyInfo(Connection* connection,
                                    ClientIntelligence intelligence,
                                    uint8_t& hashFunctionVersion,
                                    VInt& numSegments,
                                    std::vector<std::vector<uint8_t>>& segmentOwners) {
    // Helper: read vint from connection
    auto readVInt = [connection]() -> VInt {
        ByteArray vintBytes;
        while (true) {
            ByteArray byte = connection->receive(1);
            vintBytes.push_back(byte[0]);
            if ((byte[0] & 0x80) == 0) break;
        }
        size_t offset = 0;
        return Codec::readVInt(vintBytes, offset);
    };

    // Helper: read string from connection (length-prefixed)
    auto readString = [connection, &readVInt]() -> std::string {
        VInt length = readVInt();
        if (length == 0) return "";
        ByteArray strBytes = connection->receive(length);
        return std::string(strBytes.begin(), strBytes.end());
    };

    // 1. Read topology ID (vInt)
    topologyId_ = readVInt();

    // 2. Read number of servers (vInt)
    VInt numServers = readVInt();

    // 3. Read server addresses
    servers_.clear();
    servers_.reserve(numServers);

    for (VInt i = 0; i < numServers; i++) {
        ServerInfo server;

        // Read host (length-prefixed string)
        server.host = readString();

        // Read port (u2 = 16-bit unsigned, big-endian)
        ByteArray portBytes = connection->receive(2);
        server.port = (static_cast<uint16_t>(portBytes[0]) << 8) |
                      static_cast<uint16_t>(portBytes[1]);

        // Protocol 4.0: hashId NOT sent in topology - use server index
        server.hashId = static_cast<int32_t>(i);

        // Add server if not duplicate
        if (std::find(servers_.begin(), servers_.end(), server) == servers_.end()) {
            servers_.push_back(server);
        }
    }

    // Reset round-robin index
    roundRobinIndex_ = 0;

    // 4. For HASH_DISTRIBUTION_AWARE, read hash distribution info
    if (intelligence == ClientIntelligence::HASH_DISTRIBUTION_AWARE) {
        // Read hash function version (u1)
        ByteArray hashVer = connection->receive(1);
        hashFunctionVersion = hashVer[0];

        // Read number of segments (vInt)
        numSegments = readVInt();

        // Read segment owners
        segmentOwners.clear();
        segmentOwners.reserve(numSegments);

        for (VInt seg = 0; seg < numSegments; seg++) {
            // Read number of owners for this segment (u1)
            ByteArray numOwnersBytes = connection->receive(1);
            uint8_t numOwners = numOwnersBytes[0];

            // Read owner indices
            ByteArray ownerIndices = connection->receive(numOwners);
            std::vector<uint8_t> owners(ownerIndices.begin(), ownerIndices.end());

            segmentOwners.push_back(std::move(owners));
        }
    }
}

} // namespace hotrod
