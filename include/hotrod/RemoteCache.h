#pragma once

#include "Types.h"
#include "Connection.h"
#include "HeaderCodec.h"
#include <string>
#include <memory>
#include <cstdint>

namespace hotrod {

/**
 * High-level Hot Rod client API.
 *
 * Provides operations like PING, GET, PUT against an Infinispan server.
 *
 * Reference:
 * - Java: org.infinispan.client.hotrod.RemoteCache
 */
class RemoteCache {
public:
    /**
     * Create a client connected to specified server.
     *
     * @param host Server hostname or IP
     * @param port Server port (default 11222)
     */
    RemoteCache(const std::string& host, uint16_t port = 11222);

    /**
     * Create a client with optional cache name.
     */
    RemoteCache(const std::string& host, uint16_t port, const std::string& cacheName);

    ~RemoteCache();

    /**
     * Connect to the server.
     */
    void connect();

    /**
     * PING operation - test connectivity to server.
     *
     * Opcode: 0x17 (PING_REQUEST) → 0x18 (PING_RESPONSE)
     *
     * @return true if server responded successfully
     * @throws std::runtime_error if PING fails
     */
    bool ping();

    /**
     * GET operation - retrieve value for key.
     *
     * Opcode: 0x03 (GET_REQUEST) → 0x04 (GET_RESPONSE)
     *
     * @param key The key as raw bytes
     * @param value Output parameter for the value (if found)
     * @return true if key exists, false if not found
     * @throws std::runtime_error on communication errors
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.GetOperation
     */
    bool get(const ByteArray& key, ByteArray& value);

    /**
     * PUT operation - store key-value pair.
     *
     * Opcode: 0x01 (PUT_REQUEST) → 0x02 (PUT_RESPONSE)
     *
     * @param key The key as raw bytes
     * @param value The value as raw bytes
     * @param lifespan Entry lifespan in seconds (0 = immortal)
     * @param maxIdle Max idle time in seconds (0 = no max idle)
     * @param previousValue Output parameter for previous value (if existed)
     * @return true if previous value existed, false otherwise
     * @throws std::runtime_error on communication errors
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.PutOperation
     * - Java: org.infinispan.client.hotrod.impl.operations.AbstractKeyValueOperation
     */
    bool put(const ByteArray& key, const ByteArray& value,
             uint64_t lifespan = 0, uint64_t maxIdle = 0,
             ByteArray* previousValue = nullptr);

    /**
     * REMOVE operation - delete key-value pair.
     *
     * Opcode: 0x0B (REMOVE_REQUEST) → 0x0C (REMOVE_RESPONSE)
     *
     * @param key The key as raw bytes
     * @param previousValue Output parameter for previous value (if existed)
     * @return true if key existed and was removed, false if key didn't exist
     * @throws std::runtime_error on communication errors
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.RemoveOperation
     */
    bool remove(const ByteArray& key, ByteArray* previousValue = nullptr);

    /**
     * Close connection to server.
     */
    void disconnect();

    /**
     * Check if connected.
     */
    bool isConnected() const;

    /**
     * Get the cache name.
     */
    const std::string& getCacheName() const { return cacheName_; }

    /**
     * Set cache name.
     */
    void setCacheName(const std::string& name) { cacheName_ = name; }

    /**
     * Set client intelligence level.
     *
     * Controls what topology information the server sends in responses:
     * - BASIC (0x01): No topology updates, use configured server list only
     * - TOPOLOGY_AWARE (0x02): Receive topology updates, round-robin to servers
     * - HASH_DISTRIBUTION_AWARE (0x03): Topology + routing to primary owner (default in Java)
     *
     * @param intelligence The client intelligence level
     */
    void setClientIntelligence(ClientIntelligence intelligence) {
        clientIntelligence_ = intelligence;
    }

    /**
     * Get current client intelligence level.
     */
    ClientIntelligence getClientIntelligence() const {
        return clientIntelligence_;
    }

    /**
     * Get current topology information.
     * Only populated if client intelligence is TOPOLOGY_AWARE or HASH_DISTRIBUTION_AWARE.
     */
    const TopologyInfo& getTopology() const {
        return topology_;
    }

    /**
     * Get current topology ID.
     */
    VInt getTopologyId() const {
        return topology_.topologyId;
    }

private:
    std::string host_;
    uint16_t port_;
    std::string cacheName_;
    std::unique_ptr<Connection> connection_;
    uint64_t messageIdCounter_;  // For generating unique message IDs
    ClientIntelligence clientIntelligence_;  // Client intelligence level
    TopologyInfo topology_;  // Current cluster topology

    /**
     * Get next message ID.
     */
    uint64_t nextMessageId();

    /**
     * Send request and receive response.
     */
    ByteArray sendRequest(const ByteArray& request);
};

} // namespace hotrod
