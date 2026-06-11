#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace hotrod {

/**
 * Represents a single server in the cluster.
 */
struct ServerInfo {
    std::string host;
    uint16_t port;
    int32_t hashId;  // Used in Step 5 for consistent hashing

    ServerInfo() : port(0), hashId(0) {}
    ServerInfo(const std::string& h, uint16_t p, int32_t hid)
        : host(h), port(p), hashId(hid) {}

    bool operator==(const ServerInfo& other) const {
        return host == other.host && port == other.port;
    }
};

/**
 * Topology information for a cache/cluster.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.impl.TopologyInfo
 * - ROADMAP Step 4
 */
class TopologyInfo {
public:
    TopologyInfo();

    /**
     * Get current topology ID.
     */
    int getTopologyId() const { return topologyId_; }

    /**
     * Get list of all servers in the topology.
     */
    const std::vector<ServerInfo>& getServers() const { return servers_; }

    /**
     * Parse topology update from response data.
     *
     * Wire format (when topology_change_marker = 0x01):
     * - Topology ID (vInt)
     * - Number of servers (vInt)
     * - For each server:
     *   - Hostname (string = vInt length + UTF-8 bytes)
     *   - Port (uint16, 2 bytes big-endian)
     *   - Hash ID (int32, 4 bytes signed, big-endian)
     *
     * @param buffer The buffer containing topology data
     * @param offset Current read position (will be updated)
     */
    void parseTopologyUpdate(const ByteArray& buffer, size_t& offset);

    /**
     * Select a server using round-robin strategy.
     * @return Pointer to selected server, or nullptr if no servers available
     */
    const ServerInfo* selectServer();

    /**
     * Add a server to the topology.
     */
    void addServer(const ServerInfo& server);

    /**
     * Remove a server from the topology.
     */
    void removeServer(const std::string& host, uint16_t port);

    /**
     * Clear all servers.
     */
    void clearServers();

    /**
     * Check if topology has any servers.
     */
    bool hasServers() const { return !servers_.empty(); }

    /**
     * Get number of servers.
     */
    size_t getServerCount() const { return servers_.size(); }

private:
    int topologyId_;
    std::vector<ServerInfo> servers_;
    size_t roundRobinIndex_;  // For round-robin server selection
};

} // namespace hotrod
