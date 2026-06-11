#include "hotrod/TopologyInfo.h"
#include "hotrod/Codec.h"
#include <stdexcept>
#include <algorithm>

namespace hotrod {

TopologyInfo::TopologyInfo()
    : topologyId_(0), roundRobinIndex_(0) {
}

void TopologyInfo::parseTopologyUpdate(const ByteArray& buffer, size_t& offset) {
    // Read topology ID (vInt)
    topologyId_ = static_cast<int>(Codec::readVInt(buffer, offset));

    // Read number of servers (vInt)
    VInt numServers = Codec::readVInt(buffer, offset);

    // Clear existing servers
    servers_.clear();

    // Read each server
    for (VInt i = 0; i < numServers; i++) {
        ServerInfo server;

        // Read hostname (string = vInt length + UTF-8)
        server.host = Codec::readString(buffer, offset);

        // Read port (uint16, 2 bytes big-endian)
        if (offset + 2 > buffer.size()) {
            throw std::runtime_error("TopologyInfo::parseTopologyUpdate: buffer too short for port");
        }
        server.port = static_cast<uint16_t>((buffer[offset] << 8) | buffer[offset + 1]);
        offset += 2;

        // Read hash ID (int32, 4 bytes signed big-endian)
        if (offset + 4 > buffer.size()) {
            throw std::runtime_error("TopologyInfo::parseTopologyUpdate: buffer too short for hash ID");
        }
        server.hashId = static_cast<int32_t>(
            (buffer[offset] << 24) |
            (buffer[offset + 1] << 16) |
            (buffer[offset + 2] << 8) |
            buffer[offset + 3]
        );
        offset += 4;

        // Add server if not duplicate
        if (std::find(servers_.begin(), servers_.end(), server) == servers_.end()) {
            servers_.push_back(server);
        }
    }

    // Reset round-robin index
    roundRobinIndex_ = 0;
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

} // namespace hotrod
