#pragma once

#include "Types.h"
#include "Connection.h"
#include "MultiplexedConnection.h"
#include "HeaderCodec.h"
#include "ConsistentHash.h"
#include <string>
#include <memory>
#include <cstdint>
#include <map>
#include <future>
#include <optional>

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
     * @return Future that completes when PING response received
     * @throws std::runtime_error if PING fails (via future.get())
     */
    std::future<void> ping();

    /**
     * GET operation - retrieve value for key.
     *
     * Opcode: 0x03 (GET_REQUEST) → 0x04 (GET_RESPONSE)
     *
     * @param key The key as raw bytes
     * @return Future with optional<ByteArray>: value if found, nullopt if not found
     * @throws std::runtime_error on communication errors (via future.get())
     *
     * Usage:
     *   if (auto value = cache.get(key).get()) {
     *       // key found, use *value
     *   }
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.GetOperation
     */
    std::future<std::optional<ByteArray>> get(const ByteArray& key);

    /**
     * PUT operation - store key-value pair.
     *
     * Opcode: 0x01 (PUT_REQUEST) → 0x02 (PUT_RESPONSE)
     *
     * @param key The key as raw bytes
     * @param value The value as raw bytes
     * @param lifespan Entry lifespan in seconds (0 = immortal)
     * @param maxIdle Max idle time in seconds (0 = no max idle)
     * @param previousValue If true, request previous value with metadata (Protocol 4.0+)
     * @return Future with optional<EntryWithMetadata>: previous entry if existed, nullopt otherwise
     * @throws std::runtime_error on communication errors (via future.get())
     *
     * Usage:
     *   if (auto prevEntry = cache.put(key, value, 0, 0, true).get()) {
     *       // key already existed
     *       ByteArray& prevValue = prevEntry->value;
     *       int64_t version = prevEntry->metadata.version;
     *   }
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.PutOperation
     * - Java: org.infinispan.client.hotrod.impl.operations.AbstractKeyValueOperation
     */
    std::future<std::optional<EntryWithMetadata>> put(const ByteArray &key, const ByteArray &value,
                                                      uint64_t lifespan = 0, uint64_t maxIdle = 0, bool previousValue = false);

    /**
     * GET_WITH_METADATA operation - retrieve value plus entry metadata.
     *
     * Opcode: 0x1B (GET_WITH_METADATA_REQUEST) → 0x1C (GET_WITH_METADATA_RESPONSE)
     *
     * Unlike get(), this returns the entry version (for optimistic locking /
     * version-based CAS operations) and expiration metadata alongside the value.
     *
     * @param key The key as raw bytes
     * @return Future with optional<EntryWithMetadata>: entry if found, nullopt if not found
     * @throws std::runtime_error on communication errors (via future.get())
     *
     * Usage:
     *   if (auto entry = cache.getWithMetadata(key).get()) {
     *       ByteArray& value = entry->value;
     *       int64_t version = entry->metadata.version;  // for replaceWithVersion, etc.
     *   }
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.GetWithMetadataOperation
     */
    std::future<std::optional<EntryWithMetadata>> getWithMetadata(const ByteArray& key);

    /**
     * REMOVE operation - delete key-value pair.
     *
     * Opcode: 0x0B (REMOVE_REQUEST) → 0x0C (REMOVE_RESPONSE)
     *
     * @param key The key as raw bytes
     * @param previousValue If true, request previous value with metadata (Protocol 4.0+)
     * @return Future with optional<EntryWithMetadata>: previous entry if existed, nullopt otherwise
     * @throws std::runtime_error on communication errors (via future.get())
     *
     * Usage:
     *   if (auto prevEntry = cache.remove(key, true).get()) {
     *       // key existed and was removed
     *       ByteArray& prevValue = prevEntry->value;
     *       int64_t version = prevEntry->metadata.version;
     *   }
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.RemoveOperation
     */
    std::future<std::optional<EntryWithMetadata>> remove(const ByteArray& key, bool previousValue = false);

    /**
     * REMOVE_WITH_VERSION operation - version-based conditional remove (CAS).
     *
     * Removes the entry only if its current version matches `version`. The
     * version is obtained from a prior getWithMetadata() call.
     *
     * Opcode: 0x0D (REMOVE_WITH_VERSION_REQUEST) → 0x0E (RESPONSE)
     *
     * @param key The key as raw bytes
     * @param version Expected entry version (from getWithMetadata().metadata.version)
     * @return Future<bool>: true if the entry was removed; false if the version
     *         did not match (entry modified) or the key did not exist
     * @throws std::runtime_error on communication errors or unexpected status
     *         (via future.get())
     *
     * Usage:
     *   if (auto entry = cache.getWithMetadata(key).get()) {
     *       bool removed = cache.removeWithVersion(key, entry->metadata.version).get();
     *   }
     *
     * Reference:
     * - Java: org.infinispan.client.hotrod.impl.operations.RemoveIfUnmodifiedOperation
     */
    std::future<bool> removeWithVersion(const ByteArray& key, int64_t version);

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
     * Set protocol version.
     *
     * @param version Protocol version (e.g., Protocol::VERSION_40, Protocol::VERSION_41)
     */
    void setProtocolVersion(uint8_t version) {
        protocolVersion_ = version;
    }

    /**
     * Get current protocol version.
     */
    uint8_t getProtocolVersion() const {
        return protocolVersion_;
    }

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
        return topology_.getTopologyId();
    }

    /**
     * Get the ConsistentHash instance (for testing/debugging).
     */
    const ConsistentHash& getConsistentHash() const {
        return consistentHash_;
    }

private:
    std::string host_;
    uint16_t port_;
    std::string cacheName_;
    std::unique_ptr<MultiplexedConnection> connection_;
    // Note: messageIdCounter_ removed - MultiplexedConnection generates IDs
    uint8_t protocolVersion_;  // Protocol version (default: VERSION_41)
    ClientIntelligence clientIntelligence_;  // Client intelligence level
    TopologyInfo topology_;  // Current cluster topology
    ConsistentHash consistentHash_;  // Hash-aware routing (for HASH_DISTRIBUTION_AWARE)

    // Connection pool: one MultiplexedConnection per server
    // Key = "host:port", Value = MultiplexedConnection instance
    std::map<std::string, std::unique_ptr<MultiplexedConnection>> connectionPool_;

    /**
     * Handle topology update callback from MultiplexedConnection.
     */
    void handleTopologyUpdate(const TopologyInfo& topo, const std::string& cacheName);

    /**
     * Select server connection for a given key using hash-aware routing.
     *
     * When clientIntelligence == HASH_DISTRIBUTION_AWARE and hash topology is available:
     * - Calculates the segment for the key
     * - Finds the primary owner server for that segment
     * - Returns connection to that server (creating if needed)
     *
     * Otherwise falls back to the default connection.
     *
     * @param key The key to route
     * @return Connection to use for this key (never null)
     */
    MultiplexedConnection* selectServerForKey(const ByteArray& key);

    /**
     * Get or create connection for a specific server.
     *
     * @param server Server information (host, port, hashId)
     * @return MultiplexedConnection to the server (never null)
     */
    MultiplexedConnection* getConnectionForServer(const ServerInfo& server);

    /**
     * Clean up connections to servers no longer in topology.
     * Called after topology updates.
     */
    void cleanupStaleConnections();
};

} // namespace hotrod
