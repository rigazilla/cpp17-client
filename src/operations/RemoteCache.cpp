#include "hotrod/RemoteCache.h"
#include "hotrod/Codec.h"
#include <stdexcept>
#include <set>

namespace hotrod {

RemoteCache::RemoteCache(const std::string& host, uint16_t port)
    : host_(host), port_(port), cacheName_(""), messageIdCounter_(0),
      clientIntelligence_(ClientIntelligence::BASIC) {
    connection_ = std::make_unique<Connection>(host, port);
}

RemoteCache::RemoteCache(const std::string& host, uint16_t port, const std::string& cacheName)
    : host_(host), port_(port), cacheName_(cacheName), messageIdCounter_(0),
      clientIntelligence_(ClientIntelligence::BASIC) {
    connection_ = std::make_unique<Connection>(host, port);
}

RemoteCache::~RemoteCache() {
    if (isConnected()) {
        disconnect();
    }
}

void RemoteCache::connect() {
    connection_->connect();
}

void RemoteCache::disconnect() {
    connection_->close();
}

bool RemoteCache::isConnected() const {
    return connection_ && connection_->isConnected();
}

uint64_t RemoteCache::nextMessageId() {
    return ++messageIdCounter_;
}

ByteArray RemoteCache::sendRequest(const ByteArray& request) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to server");
    }

    // Use default connection
    return sendRequestToConnection(request, connection_.get());
}

ByteArray RemoteCache::sendRequestToConnection(const ByteArray& request, Connection* conn) {
    if (!conn || !conn->isConnected()) {
        throw std::runtime_error("Connection not available");
    }

    // Send request
    conn->send(request);

    // Read response header byte by byte
    // Format: magic(1) + messageId(vLong) + opcode(1) + status(1) + topologyChange(1) + [topology_data]

    ByteArray responseBuffer;

    // Read magic byte
    ByteArray magic = conn->receive(1);

    // DEBUG: Log magic byte value
    fprintf(stderr, "[DEBUG] Response magic byte: 0x%02X (expected 0xA1)\n", magic[0]);

    if (magic[0] != 0xA1) {
        fprintf(stderr, "[ERROR] Invalid magic byte received: 0x%02X\n", magic[0]);
        throw std::runtime_error("Invalid response magic byte");
    }
    responseBuffer.push_back(magic[0]);

    // Read vLong message ID (variable length)
    int msgIdBytes = 0;
    while (true) {
        ByteArray byte = conn->receive(1);
        responseBuffer.push_back(byte[0]);
        msgIdBytes++;
        // vLong continues while high bit is set
        if ((byte[0] & 0x80) == 0) {
            break;
        }
    }

    // Read opcode, status, topology change marker (3 bytes)
    ByteArray tail = conn->receive(3);
    responseBuffer.insert(responseBuffer.end(), tail.begin(), tail.end());

    uint8_t topologyMarker = tail[2];  // Last byte of tail

    // DEBUG: Log response header
    fprintf(stderr, "[DEBUG] Response header read: magic:1 + msgId:%d + opcode:1 + status:1 + topoMarker:1\n", msgIdBytes);
    fprintf(stderr, "[DEBUG] Topology marker: 0x%02X\n", topologyMarker);

    // If topology data is present, read and parse it using HeaderCodec::readTopologyInfo
    if (topologyMarker != 0 &&
        (clientIntelligence_ == ClientIntelligence::TOPOLOGY_AWARE ||
         clientIntelligence_ == ClientIntelligence::HASH_DISTRIBUTION_AWARE)) {

        fprintf(stderr, "[DEBUG] Reading topology data (intelligence: 0x%02X)...\n",
                static_cast<uint8_t>(clientIntelligence_));

        try {
            // Variables for hash distribution (only used if HASH_DISTRIBUTION_AWARE)
            uint8_t hashFunctionVersion = 0;
            VInt numSegments = 0;
            std::vector<std::vector<uint8_t>> segmentOwners;

            // Read topology directly from connection
            topology_.parseTopologyInfo(conn, clientIntelligence_,
                                       hashFunctionVersion, numSegments, segmentOwners);

            fprintf(stderr, "[DEBUG] Topology parsed: ID=%d, servers=%zu\n",
                    topology_.getTopologyId(), topology_.getServers().size());

            // Log servers
            for (const auto& server : topology_.getServers()) {
                fprintf(stderr, "[DEBUG]   Server: %s:%u (hashId=%d)\n",
                        server.host.c_str(), server.port, server.hashId);
            }

            // If HASH_DISTRIBUTION_AWARE, parse hash topology into ConsistentHash
            if (clientIntelligence_ == ClientIntelligence::HASH_DISTRIBUTION_AWARE &&
                numSegments > 0) {

                fprintf(stderr, "[DEBUG] Parsing hash topology: %d segments, hashFn=%d\n",
                        numSegments, hashFunctionVersion);

                // Convert owner indices to hashIds
                // segmentOwners[segment][ownerIndex] contains indices into topology servers
                // We need to convert to hashIds for ConsistentHash
                std::vector<std::vector<int32_t>> segmentOwnerHashIds;
                segmentOwnerHashIds.resize(segmentOwners.size());

                for (size_t segment = 0; segment < segmentOwners.size(); segment++) {
                    for (uint8_t ownerIdx : segmentOwners[segment]) {
                        if (ownerIdx < topology_.getServers().size()) {
                            int32_t hashId = topology_.getServers()[ownerIdx].hashId;
                            segmentOwnerHashIds[segment].push_back(hashId);
                        } else {
                            fprintf(stderr, "[WARN] Invalid owner index %d for segment %zu\n",
                                    ownerIdx, segment);
                        }
                    }
                }

                // Create a buffer and encode the hash topology for ConsistentHash::parseHashTopology
                ByteArray hashTopoBuffer;
                Codec::writeVInt(hashTopoBuffer, numSegments);

                // Write number of owners (assume all segments have same number of owners)
                uint8_t numOwners = !segmentOwnerHashIds.empty() && !segmentOwnerHashIds[0].empty()
                                    ? segmentOwnerHashIds[0].size() : 0;
                hashTopoBuffer.push_back(numOwners);

                // Write segment ownership (hashIds in big-endian int32)
                for (const auto& owners : segmentOwnerHashIds) {
                    for (int32_t hashId : owners) {
                        // Write as signed int32, big-endian (4 bytes)
                        hashTopoBuffer.push_back((hashId >> 24) & 0xFF);
                        hashTopoBuffer.push_back((hashId >> 16) & 0xFF);
                        hashTopoBuffer.push_back((hashId >> 8) & 0xFF);
                        hashTopoBuffer.push_back(hashId & 0xFF);
                    }
                }

                // Parse into ConsistentHash
                size_t offset = 0;
                consistentHash_.parseHashTopology(hashTopoBuffer, offset, topology_);

                fprintf(stderr, "[DEBUG] ConsistentHash initialized: %d segments, %d owners\n",
                        consistentHash_.getNumSegments(), consistentHash_.getNumOwners());

                // Clean up connections to servers no longer in topology
                cleanupStaleConnections();
            }

        } catch (const std::exception& e) {
            fprintf(stderr, "[WARN] Failed to parse topology: %s\n", e.what());
        }
    }

    fprintf(stderr, "[DEBUG] Total response bytes: %zu\n", responseBuffer.size());

    return responseBuffer;
}

bool RemoteCache::ping() {
    // Build PING request header
    RequestHeader header;
    header.messageId = nextMessageId();
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x17;   // PING_REQUEST
    header.cacheName = cacheName_;
    header.flags = 0;
    header.clientIntelligence = clientIntelligence_;
    header.topologyId = topology_.getTopologyId();
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty (count = 0)

    // Encode request
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Send and receive response
    ByteArray response = sendRequest(request);

    // Parse response header
    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    // Verify response
    if (respHeader.opcode != 0x18) {  // PING_RESPONSE
        throw std::runtime_error("Unexpected response opcode: " + std::to_string(respHeader.opcode));
    }

    if (respHeader.messageId != header.messageId) {
        throw std::runtime_error("Message ID mismatch");
    }

    if (respHeader.status != 0x00) {  // NO_ERROR
        throw std::runtime_error("PING failed with status: " + std::to_string(respHeader.status));
    }

    // PING response body (Protocol 3.0+):
    // - key_type (media_type)
    // - value_type (media_type)
    // - server_version (u1)
    // - op_count (vint)
    // - supported_opcodes (u2 array)
    //
    // We need to consume the ENTIRE response body to clear the socket buffer!

    // Read key_type media_type (at minimum: type_indicator=1 byte)
    ByteArray keyType = connection_->receive(1);
    uint8_t keyTypeIndicator = keyType[0];

    // If not NONE (0), read param_count and params
    if (keyTypeIndicator != 0) {
        // Read predefined_id or custom_string based on indicator
        if (keyTypeIndicator == 1) {  // predefined
            // Read vint predefined_id
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) break;
            }
        } else if (keyTypeIndicator == 2) {  // custom
            // Read lp_string (vint length + bytes)
            VInt len = 0;
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) {
                    len = b[0]; // simplified
                    break;
                }
            }
            if (len > 0) connection_->receive(len);
        }
        // Read param_count (vint)
        VInt paramCount = 0;
        while (true) {
            ByteArray b = connection_->receive(1);
            if ((b[0] & 0x80) == 0) {
                paramCount = b[0]; // simplified
                break;
            }
        }
        (void)paramCount; // TODO: Read params if paramCount > 0
    }

    // Read value_type media_type (same structure as key_type)
    ByteArray valType = connection_->receive(1);
    uint8_t valTypeIndicator = valType[0];
    if (valTypeIndicator != 0) {
        if (valTypeIndicator == 1) {
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) break;
            }
        } else if (valTypeIndicator == 2) {
            VInt len = 0;
            while (true) {
                ByteArray b = connection_->receive(1);
                if ((b[0] & 0x80) == 0) {
                    len = b[0];
                    break;
                }
            }
            if (len > 0) connection_->receive(len);
        }
        VInt paramCount = 0;
        while (true) {
            ByteArray b = connection_->receive(1);
            if ((b[0] & 0x80) == 0) {
                paramCount = b[0];
                break;
            }
        }
        (void)paramCount; // TODO: Read params if paramCount > 0
    }

    // Read server_version (1 byte)
    connection_->receive(1);

    // Read op_count (vint)
    VInt opCount = 0;
    while (true) {
        ByteArray b = connection_->receive(1);
        opCount = (opCount << 7) | (b[0] & 0x7F);
        if ((b[0] & 0x80) == 0) break;
    }

    // Read supported_opcodes array (opCount * 2 bytes each = u2)
    if (opCount > 0) {
        connection_->receive(opCount * 2);
    }

    fprintf(stderr, "[DEBUG] PING response body consumed successfully\n");

    return true;
}

bool RemoteCache::get(const ByteArray& key, ByteArray& value) {
    // Build GET request header
    RequestHeader header;
    header.messageId = nextMessageId();
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x03;   // GET_REQUEST
    header.cacheName = cacheName_;
    header.flags = 0;
    header.clientIntelligence = clientIntelligence_;
    header.topologyId = topology_.getTopologyId();
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty (count = 0)

    // Encode request header + key
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Write key as lp_bytes (vInt length + bytes)
    Codec::writeByteArray(request, key);

    // Send and receive response (with automatic failover)
    auto [response, targetConn] = sendRequestWithFailover(request, key);

    // Parse response header
    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    // Verify response
    if (respHeader.opcode != 0x04) {  // GET_RESPONSE
        throw std::runtime_error("Unexpected response opcode: " + std::to_string(respHeader.opcode));
    }

    if (respHeader.messageId != header.messageId) {
        throw std::runtime_error("Message ID mismatch");
    }

    // Status codes:
    // 0x00 = NO_ERROR (key found)
    // 0x01 = KEY_DOES_NOT_EXIST
    // 0x02 = NOT_FOUND (cache-level not found, different from key not found)
    if (respHeader.status == 0x01 || respHeader.status == 0x02) {
        // Key not found
        value.clear();
        return false;
    }

    if (respHeader.status != 0x00) {
        throw std::runtime_error("GET failed with status: " + std::to_string(respHeader.status));
    }

    // Read value from response body (lp_bytes: vInt length + bytes)
    // The response body is NOT in the response ByteArray - we only read the header!
    // Need to read the value directly from the socket.

    // Read vInt length (same algorithm as Codec::readVInt but from socket)
    ByteArray firstByte = targetConn->receive(1);
    VInt valueLength = firstByte[0] & 0x7F;

    for (int shift = 7; (firstByte[0] & 0x80) != 0; shift += 7) {
        ByteArray nextByte = targetConn->receive(1);
        valueLength |= static_cast<VInt>(nextByte[0] & 0x7F) << shift;
        firstByte[0] = nextByte[0];  // Update for continuation check
    }

    // Read value bytes
    if (valueLength > 0) {
        value = targetConn->receive(valueLength);
    } else {
        value.clear();
    }

    return true;
}

bool RemoteCache::put(const ByteArray& key, const ByteArray& value,
                      uint64_t lifespan, uint64_t maxIdle,
                      ByteArray* previousValue) {
    // Build PUT request header
    RequestHeader header;
    header.messageId = nextMessageId();
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x01;   // PUT_REQUEST
    header.cacheName = cacheName_;
    header.flags = 0;
    header.clientIntelligence = clientIntelligence_;
    header.topologyId = topology_.getTopologyId();
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty (count = 0)

    // Encode request header + key + expiration + value
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Write key as lp_bytes (vInt length + bytes)
    Codec::writeByteArray(request, key);

    // Write expiration parameters (time_units byte + optional lifespan/maxIdle)
    // Time units encoding (per Protocol 3.0+):
    // - High nibble (bits 4-7): lifespan time unit
    // - Low nibble (bits 0-3): maxIdle time unit
    // - 0x00 = SECONDS, 0x07 = DEFAULT (infinite/server default)
    // - If unit < 0x07, the duration (vLong) follows

    uint8_t timeUnits = 0;
    if (lifespan == 0) {
        timeUnits |= (0x07 << 4);  // DEFAULT (infinite)
    } else {
        timeUnits |= (0x00 << 4);  // SECONDS
    }
    if (maxIdle == 0) {
        timeUnits |= 0x07;  // DEFAULT (infinite)
    } else {
        timeUnits |= 0x00;  // SECONDS
    }
    request.push_back(timeUnits);

    // Write lifespan if not default
    if (lifespan > 0) {
        Codec::writeVLong(request, lifespan);
    }

    // Write maxIdle if not default
    if (maxIdle > 0) {
        Codec::writeVLong(request, maxIdle);
    }

    // Write value as lp_bytes (vInt length + bytes)
    Codec::writeByteArray(request, value);

    // Send and receive response (with automatic failover)
    auto [response, targetConn] = sendRequestWithFailover(request, key);

    // Parse response header
    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    // Verify response
    if (respHeader.opcode != 0x02) {  // PUT_RESPONSE
        fprintf(stderr, "[ERROR] PUT response opcode: 0x%02X (expected 0x02), status: 0x%02X\n",
                respHeader.opcode, respHeader.status);
        throw std::runtime_error("Unexpected response opcode: " + std::to_string(respHeader.opcode));
    }

    if (respHeader.messageId != header.messageId) {
        throw std::runtime_error("Message ID mismatch");
    }

    if (respHeader.status != 0x00) {  // NO_ERROR
        throw std::runtime_error("PUT failed with status: " + std::to_string(respHeader.status));
    }

    // Check if previous value exists (status codes 0x03 or 0x04 in Protocol 4.0)
    // For Protocol 4.0, PUT response includes metadata + value if previous existed
    // But for simple PUT, we check the response body
    // If there's data after the header, it's the previous value

    // Read previous value from response body if present
    // The response body structure depends on whether a previous value existed
    // For now, assume no previous value (status 0x00 with no body)
    // TODO: Handle previous value response in Protocol 4.0

    if (previousValue) {
        // Try to read previous value (may be empty)
        // Check if there's more data in the socket
        // For now, just clear it
        previousValue->clear();
    }

    return false;  // No previous value (for now)
}

bool RemoteCache::remove(const ByteArray& key, ByteArray* previousValue) {
    // Build REMOVE request header
    RequestHeader header;
    header.messageId = nextMessageId();
    header.version = 0x28;  // Protocol 4.0
    header.opcode = 0x0B;   // REMOVE_REQUEST
    header.cacheName = cacheName_;
    header.flags = 0;
    header.clientIntelligence = clientIntelligence_;
    header.topologyId = topology_.getTopologyId();
    header.keyMediaType = 0;
    header.valueMediaType = 0;
    // otherParams empty (count = 0)

    // Encode request header + key
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);

    // Write key as lp_bytes (vInt length + bytes)
    Codec::writeByteArray(request, key);

    // Send and receive response (with automatic failover)
    auto [response, targetConn] = sendRequestWithFailover(request, key);

    // Parse response header
    size_t offset = 0;
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);

    // Verify response
    if (respHeader.opcode != 0x0C) {  // REMOVE_RESPONSE
        fprintf(stderr, "[ERROR] REMOVE response opcode: 0x%02X (expected 0x0C), status: 0x%02X\n",
                respHeader.opcode, respHeader.status);
        throw std::runtime_error("Unexpected response opcode: " + std::to_string(respHeader.opcode));
    }

    if (respHeader.messageId != header.messageId) {
        throw std::runtime_error("Message ID mismatch");
    }

    // Status codes:
    // 0x00 = SUCCESS (key existed, no previous value in response)
    // 0x01 = NOT_EXECUTED (operation not executed)
    // 0x02 = KEY_DOES_NOT_EXIST (key didn't exist)
    // 0x03 = SUCCESS_WITH_PREVIOUS (key existed, previous value in response)
    // 0x04 = NOT_EXECUTED_WITH_PREVIOUS

    if (respHeader.status == 0x01 || respHeader.status == 0x02) {
        // Key didn't exist
        if (previousValue) {
            previousValue->clear();
        }
        return false;
    }

    // Check if previous value is included in response
    bool hasPreviousValue = (respHeader.status == 0x03 || respHeader.status == 0x04);

    if (respHeader.status != 0x00 && respHeader.status != 0x03 && respHeader.status != 0x04) {
        throw std::runtime_error("REMOVE failed with status: " + std::to_string(respHeader.status));
    }

    // Read previous value from response body if status indicates it's present
    if (hasPreviousValue) {
        // Read vInt length (same algorithm as GET)
        ByteArray firstByte = targetConn->receive(1);
        VInt valueLength = firstByte[0] & 0x7F;

        for (int shift = 7; (firstByte[0] & 0x80) != 0; shift += 7) {
            ByteArray nextByte = targetConn->receive(1);
            valueLength |= static_cast<VInt>(nextByte[0] & 0x7F) << shift;
            firstByte[0] = nextByte[0];  // Update for continuation check
        }

        // Read previous value bytes
        if (previousValue) {
            if (valueLength > 0) {
                *previousValue = targetConn->receive(valueLength);
            } else {
                previousValue->clear();
            }
        } else {
            // Discard the value if caller doesn't want it
            if (valueLength > 0) {
                targetConn->receive(valueLength);
            }
        }
    } else {
        // No previous value in response (status 0x00)
        if (previousValue) {
            previousValue->clear();
        }
    }

    return true;  // Key existed and was removed
}

Connection* RemoteCache::selectServerForKey(const ByteArray& key) {
    // If hash-aware routing is enabled and hash topology is available
    if (clientIntelligence_ == ClientIntelligence::HASH_DISTRIBUTION_AWARE &&
        consistentHash_.hasHashTopology()) {

        int segment = consistentHash_.getSegment(key);

        // Get all owners (primary + backups) for this key
        auto owners = consistentHash_.getOwners(key, topology_);

        if (!owners.empty()) {
            fprintf(stderr, "[DEBUG] Hash-aware routing: key → segment %d → trying %zu owner(s)\n",
                    segment, owners.size());

            // Try each owner in order (primary first, then backups)
            for (size_t i = 0; i < owners.size(); i++) {
                const ServerInfo* owner = owners[i];
                try {
                    Connection* conn = getConnectionForServer(*owner);
                    if (i == 0) {
                        fprintf(stderr, "[DEBUG] Routing to primary owner: %s:%u\n",
                                owner->host.c_str(), owner->port);
                    } else {
                        fprintf(stderr, "[DEBUG] Failover to backup owner #%zu: %s:%u\n",
                                i, owner->host.c_str(), owner->port);
                    }
                    return conn;
                } catch (const std::exception& e) {
                    fprintf(stderr, "[WARN] Failed to connect to owner %s:%u: %s\n",
                            owner->host.c_str(), owner->port, e.what());
                    // Continue to next owner
                }
            }

            // All owners failed, try any server in topology
            fprintf(stderr, "[WARN] All owners failed for segment %d, trying any server\n", segment);
            const auto& servers = topology_.getServers();
            for (const auto& server : servers) {
                // Skip servers we already tried as owners
                bool alreadyTried = false;
                for (const auto* owner : owners) {
                    if (server.host == owner->host && server.port == owner->port) {
                        alreadyTried = true;
                        break;
                    }
                }
                if (alreadyTried) continue;

                try {
                    Connection* conn = getConnectionForServer(server);
                    fprintf(stderr, "[DEBUG] Failover to non-owner server: %s:%u\n",
                            server.host.c_str(), server.port);
                    return conn;
                } catch (const std::exception& e) {
                    fprintf(stderr, "[WARN] Failed to connect to server %s:%u: %s\n",
                            server.host.c_str(), server.port, e.what());
                    // Continue to next server
                }
            }

            // No servers available
            throw std::runtime_error("No servers available in topology");
        } else {
            fprintf(stderr, "[WARN] No owners found for key (segment %d), falling back to default connection\n", segment);
        }
    }

    // Fallback: use default connection
    return connection_.get();
}

std::pair<ByteArray, Connection*> RemoteCache::sendRequestWithFailover(const ByteArray& request, const ByteArray& key) {
    // If hash-aware routing is enabled
    if (clientIntelligence_ == ClientIntelligence::HASH_DISTRIBUTION_AWARE &&
        consistentHash_.hasHashTopology()) {

        int segment = consistentHash_.getSegment(key);
        auto owners = consistentHash_.getOwners(key, topology_);

        // Try each owner (primary + backups)
        for (size_t i = 0; i < owners.size(); i++) {
            const ServerInfo* owner = owners[i];
            try {
                Connection* conn = getConnectionForServer(*owner);
                if (i == 0) {
                    fprintf(stderr, "[DEBUG] Routing to primary owner: %s:%u (segment %d)\n",
                            owner->host.c_str(), owner->port, segment);
                } else {
                    fprintf(stderr, "[DEBUG] Failover to backup #%zu: %s:%u (segment %d)\n",
                            i, owner->host.c_str(), owner->port, segment);
                }
                ByteArray response = sendRequestToConnection(request, conn);
                return {response, conn};
            } catch (const std::exception& e) {
                fprintf(stderr, "[WARN] Request failed to owner %s:%u: %s\n",
                        owner->host.c_str(), owner->port, e.what());
                // Continue to next owner
            }
        }

        // All owners failed, try any server
        fprintf(stderr, "[WARN] All owners failed for segment %d, trying any server\n", segment);
        const auto& servers = topology_.getServers();
        for (const auto& server : servers) {
            // Skip servers we already tried
            bool alreadyTried = false;
            for (const auto* owner : owners) {
                if (server.host == owner->host && server.port == owner->port) {
                    alreadyTried = true;
                    break;
                }
            }
            if (alreadyTried) continue;

            try {
                Connection* conn = getConnectionForServer(server);
                fprintf(stderr, "[DEBUG] Failover to non-owner: %s:%u\n",
                        server.host.c_str(), server.port);
                ByteArray response = sendRequestToConnection(request, conn);
                return {response, conn};
            } catch (const std::exception& e) {
                fprintf(stderr, "[WARN] Request failed to server %s:%u: %s\n",
                        server.host.c_str(), server.port, e.what());
                // Continue to next server
            }
        }

        throw std::runtime_error("No servers available - all failed");
    }

    // Fallback: use default connection (no hash-aware routing)
    ByteArray response = sendRequestToConnection(request, connection_.get());
    return {response, connection_.get()};
}

Connection* RemoteCache::getConnectionForServer(const ServerInfo& server) {
    // Create connection pool key
    std::string poolKey = server.host + ":" + std::to_string(server.port);

    // Check if connection already exists in pool
    auto it = connectionPool_.find(poolKey);
    if (it != connectionPool_.end()) {
        // Connection exists, check if it's still connected
        if (it->second->isConnected()) {
            return it->second.get();
        } else {
            fprintf(stderr, "[DEBUG] Reconnecting to %s\n", poolKey.c_str());
            it->second->connect();
            return it->second.get();
        }
    }

    // Connection doesn't exist, create new one
    fprintf(stderr, "[DEBUG] Creating new connection to %s\n", poolKey.c_str());
    auto newConnection = std::make_unique<Connection>(server.host, server.port);
    newConnection->connect();

    Connection* connPtr = newConnection.get();
    connectionPool_[poolKey] = std::move(newConnection);

    return connPtr;
}

void RemoteCache::cleanupStaleConnections() {
    // Build set of current servers
    std::set<std::string> currentServers;
    for (const auto& server : topology_.getServers()) {
        std::string key = server.host + ":" + std::to_string(server.port);
        currentServers.insert(key);
    }

    // Remove connections not in current topology
    auto it = connectionPool_.begin();
    while (it != connectionPool_.end()) {
        if (currentServers.find(it->first) == currentServers.end()) {
            fprintf(stderr, "[DEBUG] Removing stale connection to %s\n", it->first.c_str());
            it = connectionPool_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace hotrod
