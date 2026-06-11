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

private:
    std::string host_;
    uint16_t port_;
    std::string cacheName_;
    std::unique_ptr<Connection> connection_;
    uint64_t messageIdCounter_;  // For generating unique message IDs

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
