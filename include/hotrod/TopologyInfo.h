#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace hotrod {

// Forward declarations
class Connection;
enum class ClientIntelligence : uint8_t;

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
     * Set topology ID (for testing).
     */
    void setTopologyId(int id) { topologyId_ = id; }

    /**
     * Get list of all servers in the topology.
     */
    const std::vector<ServerInfo>& getServers() const { return servers_; }

    /**
     * Read and parse topology update from connection.
     *
     * Reads topology and optionally hash distribution data directly from connection.
     * Used when topology marker is set in response header.
     *
     * @param connection Connection to read from
     * @param intelligence Client intelligence level
     * @param hashFunctionVersion Output: hash function version (for HASH_DISTRIBUTION_AWARE)
     * @param numSegments Output: number of segments (for HASH_DISTRIBUTION_AWARE)
     * @param segmentOwners Output: segment ownership (for HASH_DISTRIBUTION_AWARE)
     */
    void parseTopologyInfo(Connection* connection,
                          ClientIntelligence intelligence,
                          uint8_t& hashFunctionVersion,
                          VInt& numSegments,
                          std::vector<std::vector<uint8_t>>& segmentOwners);

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

    /**
     * Get hash function version.
     */
    uint8_t getHashFunctionVersion() const { return hashFunctionVersion_; }

    /**
     * Get number of segments.
     */
    VInt getNumSegments() const { return numSegments_; }

    /**
     * Get segment owners.
     * segmentOwners_[segment][ownerIndex] = server hash ID (uint8_t index into servers_)
     */
    const std::vector<std::vector<uint8_t>>& getSegmentOwners() const { return segmentOwners_; }

    /**
     * Check if hash topology data is available.
     */
    bool hasHashTopology() const { return numSegments_ > 0; }

private:
    int topologyId_;
    std::vector<ServerInfo> servers_;
    size_t roundRobinIndex_;  // For round-robin server selection

    // Hash topology data (only populated when ClientIntelligence == HASH_DISTRIBUTION_AWARE)
    uint8_t hashFunctionVersion_{0};
    VInt numSegments_{0};
    std::vector<std::vector<uint8_t>> segmentOwners_;  // [segment][ownerIndex] = server hash ID
};

} // namespace hotrod
