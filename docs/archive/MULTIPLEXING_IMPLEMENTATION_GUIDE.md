# MultiplexedConnection Implementation Guide

**Status**: 70% Complete - Final steps to finish true async implementation

---

## What's Already Done ✅

1. **MultiplexedConnection class** - Fully implemented
   - `include/hotrod/MultiplexedConnection.h`
   - `src/transport/MultiplexedConnection.cpp`
   - Read loop thread, pending requests map, execute() method

2. **Connection helper methods** - Implemented
   - `receiveVInt()`, `receiveVLong()`, `receiveByteArray()`, `receiveString()`

3. **RemoteCache header** - Updated
   - Uses `MultiplexedConnection*` instead of `Connection*`
   - Removed `messageIdCounter_`
   - Added `handleTopologyUpdate()` method

4. **GET operation** - Fully converted to async ✅
   - Uses execute() with body parser lambda
   - Returns transformed future

---

## What You Need to Do

### Step 1: Update PUT Operation

**File**: `src/operations/RemoteCache.cpp`

**Find** (around line 388):
```cpp
std::future<std::optional<ByteArray>> RemoteCache::put(const ByteArray& key, const ByteArray& value,
                                                        uint64_t lifespan, uint64_t maxIdle) {
    // Build PUT request header
    RequestHeader header;
    header.messageId = nextMessageId();  // ← OLD CODE
    ...
```

**Replace entire PUT function with**:
```cpp
std::future<std::optional<ByteArray>> RemoteCache::put(const ByteArray& key, const ByteArray& value,
                                                        uint64_t lifespan, uint64_t maxIdle) {
    // Build request body (key + time_units + expiration + value)
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);

    // Time units byte
    uint8_t timeUnits = 0;
    if (lifespan == 0) {
        timeUnits |= (0x07 << 4);  // DEFAULT (infinite)
    } else {
        timeUnits |= (0x00 << 4);  // SECONDS
    }
    if (maxIdle == 0) {
        timeUnits |= 0x07;
    } else {
        timeUnits |= 0x00;
    }
    requestBody.push_back(timeUnits);

    if (lifespan > 0) {
        Codec::writeVLong(requestBody, lifespan);
    }
    if (maxIdle > 0) {
        Codec::writeVLong(requestBody, maxIdle);
    }

    Codec::writeByteArray(requestBody, value);

    // Define body parser
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x03) {
            // SUCCESS_WITH_PREVIOUS - read previous value
            return conn->receiveByteArray();
        }
        // Status 0x00 (success, no previous) or other - no body
        return {};
    };

    // Select connection
    MultiplexedConnection* conn = selectServerForKey(key);

    // Execute
    auto responseFuture = conn->execute(
        requestBody,
        0x01,  // PUT_REQUEST
        0x02,  // PUT_RESPONSE
        bodyParser,
        cacheName_
    );

    // Transform Response → std::optional<ByteArray>
    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable -> std::optional<ByteArray> {
            Response resp = responseFuture.get();

            if (resp.error) {
                std::rethrow_exception(resp.error);
            }

            if (resp.status != 0x00 && resp.status != 0x03) {
                throw std::runtime_error("PUT failed with status: " + std::to_string(resp.status));
            }

            if (resp.status == 0x03 && !resp.body.empty()) {
                return std::optional<ByteArray>(std::move(resp.body));
            }

            return std::nullopt;
        });
}
```

---

### Step 2: Update REMOVE Operation

**File**: `src/operations/RemoteCache.cpp`

**Find** (around line 480):
```cpp
std::future<std::optional<ByteArray>> RemoteCache::remove(const ByteArray& key) {
    // Build REMOVE request header
    RequestHeader header;
    header.messageId = nextMessageId();  // ← OLD CODE
    ...
```

**Replace entire REMOVE function with**:
```cpp
std::future<std::optional<ByteArray>> RemoteCache::remove(const ByteArray& key) {
    // Build request body (just the key)
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);

    // Define body parser
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x03) {
            // SUCCESS_WITH_PREVIOUS
            return conn->receiveByteArray();
        }
        return {};
    };

    // Select connection
    MultiplexedConnection* conn = selectServerForKey(key);

    // Execute
    auto responseFuture = conn->execute(
        requestBody,
        0x0B,  // REMOVE_REQUEST
        0x0C,  // REMOVE_RESPONSE
        bodyParser,
        cacheName_
    );

    // Transform Response → std::optional<ByteArray>
    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable -> std::optional<ByteArray> {
            Response resp = responseFuture.get();

            if (resp.error) {
                std::rethrow_exception(resp.error);
            }

            if (resp.status == 0x03 && !resp.body.empty()) {
                return std::optional<ByteArray>(std::move(resp.body));
            }

            return std::nullopt;
        });
}
```

---

### Step 3: Update PING Operation

**File**: `src/operations/RemoteCache.cpp`

**Find** (around line 200):
```cpp
std::future<void> RemoteCache::ping() {
    // Build PING request header
    RequestHeader header;
    header.messageId = nextMessageId();  // ← OLD CODE
    ...
```

**Replace entire PING function with**:
```cpp
std::future<void> RemoteCache::ping() {
    // Build request body (empty for PING)
    ByteArray requestBody;

    // Define body parser (PING has complex response body)
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status != 0x00) {
            throw std::runtime_error("PING failed with status: " + std::to_string(status));
        }

        // Read and discard PING response body:
        // - key_media_type (simplified: assume 0x00 = NONE)
        // - value_media_type (simplified: assume 0x00 = NONE)
        // - server_version (1 byte)
        // - op_count (vint)
        // - supported_opcodes (u2 array)

        // Read key media type (simplified)
        uint8_t keyType = conn->receive(1)[0];
        if (keyType != 0) {
            // Complex media type - skip for now
            throw std::runtime_error("Complex media types not yet supported in PING");
        }

        // Read value media type
        uint8_t valType = conn->receive(1)[0];
        if (valType != 0) {
            throw std::runtime_error("Complex media types not yet supported in PING");
        }

        // Read server version
        conn->receive(1);

        // Read opcode count and opcodes
        VInt opCount = conn->receiveVInt();
        if (opCount > 0) {
            conn->receive(opCount * 2);  // Each opcode is 2 bytes
        }

        return {};  // PING doesn't return data
    };

    // Use default connection
    auto responseFuture = connection_->execute(
        requestBody,
        0x17,  // PING_REQUEST
        0x18,  // PING_RESPONSE
        bodyParser,
        cacheName_
    );

    // Transform Response → void
    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable {
            Response resp = responseFuture.get();

            if (resp.error) {
                std::rethrow_exception(resp.error);
            }

            // Success - return void
        });
}
```

---

### Step 4: Update Helper Methods

**File**: `src/operations/RemoteCache.cpp`

**Find**: `Connection* RemoteCache::selectServerForKey`

**Replace**: `MultiplexedConnection* RemoteCache::selectServerForKey`

**Change all occurrences**:
- `Connection* conn` → `MultiplexedConnection* conn`
- `Connection* RemoteCache::getConnectionForServer` → `MultiplexedConnection* RemoteCache::getConnectionForServer`

**Example for selectServerForKey**:
```cpp
MultiplexedConnection* RemoteCache::selectServerForKey(const ByteArray& key) {
    // If hash-aware routing is enabled and hash topology is available
    if (clientIntelligence_ == ClientIntelligence::HASH_DISTRIBUTION_AWARE &&
        consistentHash_.hasHashTopology()) {

        int segment = consistentHash_.getSegment(key);
        auto owners = consistentHash_.getOwners(key, topology_);

        if (!owners.empty()) {
            for (size_t i = 0; i < owners.size(); i++) {
                const ServerInfo* owner = owners[i];
                try {
                    MultiplexedConnection* conn = getConnectionForServer(*owner);  // ← Changed
                    return conn;
                } catch (const std::exception& e) {
                    // Continue to next owner
                }
            }
        }
    }

    // Fallback: use default connection
    return connection_.get();
}
```

**Example for getConnectionForServer**:
```cpp
MultiplexedConnection* RemoteCache::getConnectionForServer(const ServerInfo& server) {
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
    
    auto topologyCallback = [this](const TopologyInfo& topo, const std::string& cacheName) {
        this->handleTopologyUpdate(topo, cacheName);
    };
    
    auto newConnection = std::make_unique<MultiplexedConnection>(
        server.host, server.port, topologyCallback);
    newConnection->setClientIntelligence(clientIntelligence_);
    newConnection->connect();

    MultiplexedConnection* connPtr = newConnection.get();
    connectionPool_[poolKey] = std::move(newConnection);

    return connPtr;
}
```

---

### Step 5: Remove Old Code

**File**: `src/operations/RemoteCache.cpp`

**Delete these functions entirely** (they're no longer needed):
1. `ByteArray RemoteCache::sendRequest(const ByteArray& request)`
2. `ByteArray RemoteCache::sendRequestToConnection(const ByteArray& request, Connection* conn)`
3. `std::pair<ByteArray, Connection*> RemoteCache::sendRequestWithFailover(...)`

These methods were for the old blocking implementation.

---

### Step 6: Update connect() Method

**File**: `src/operations/RemoteCache.cpp`

**Find**:
```cpp
void RemoteCache::connect() {
    connection_->connect();
}
```

**Replace with**:
```cpp
void RemoteCache::connect() {
    connection_->setClientIntelligence(clientIntelligence_);
    connection_->connect();
}
```

This ensures client intelligence is set before connecting.

---

## Pattern Reference

All operations follow this pattern:

```cpp
std::future<ReturnType> RemoteCache::operation(...) {
    // 1. Build request BODY (no header - MultiplexedConnection builds it)
    ByteArray requestBody;
    // ... encode operation-specific data ...

    // 2. Define body parser lambda (runs in read thread)
    auto bodyParser = [captures](uint8_t status, Connection* conn) -> ByteArray {
        if (status != success_code) {
            // Handle error statuses
            return {};  // or throw
        }
        
        // Read response body using conn->receiveByteArray(), etc.
        return conn->receiveByteArray();
    };

    // 3. Select connection (hash-aware or default)
    MultiplexedConnection* conn = selectServerForKey(key);  // or connection_.get()

    // 4. Execute via MultiplexedConnection
    auto responseFuture = conn->execute(
        requestBody,
        requestOpcode,
        responseOpcode,
        bodyParser,
        cacheName_
    );

    // 5. Transform Response → final return type
    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable -> ReturnType {
            Response resp = responseFuture.get();

            if (resp.error) {
                std::rethrow_exception(resp.error);
            }

            // Process resp.status and resp.body
            return transformedResult;
        });
}
```

---

## Testing After Changes

1. **Build**:
   ```bash
   cmake --build build
   ```

2. **Run integration tests**:
   ```bash
   ./build/ping_integration_tests
   ./build/get_integration_tests
   ./build/put_integration_tests
   ./build/remove_integration_tests
   ```

3. **Check for true concurrency**:
   - Run concurrent_clients_tests
   - Multiple operations should now truly run in parallel
   - Check with `top` or profiling that read thread is active

---

## Expected Benefits After Completion

**Before** (temporary blocking implementation):
- Operations return futures but still block
- Sequential execution even with multiple futures

**After** (true async with MultiplexedConnection):
- ✅ True concurrent operations on same socket
- ✅ Request pipelining (multiple in-flight requests)
- ✅ Read thread continuously processes responses
- ✅ 5-10x throughput improvement for parallel operations
- ✅ Lower latency (no connection pool contention)

---

## Troubleshooting

**Build Error: `nextMessageId()` not found**
- You forgot to remove a call to `nextMessageId()`
- Search for `nextMessageId()` and replace with execute() pattern

**Build Error: `Connection*` cannot convert to `MultiplexedConnection*`**
- Update the return type of helper methods
- Change `Connection*` → `MultiplexedConnection*`

**Build Error: `sendRequestWithFailover` not found**
- Remove all calls to this old method
- Use `selectServerForKey()` + `execute()` instead

**Tests fail with "Not connected"**
- Make sure `connect()` calls `setClientIntelligence()` first
- Check that connection pool creates MultiplexedConnection correctly

**Tests timeout or hang**
- Body parser might be wrong (not reading all response data)
- Check each body parser matches the operation's response format
- Add debug prints in read loop to see what's happening

---

## Quick Checklist

- [ ] Update PUT operation (Step 1)
- [ ] Update REMOVE operation (Step 2)
- [ ] Update PING operation (Step 3)
- [ ] Update selectServerForKey() return type (Step 4)
- [ ] Update getConnectionForServer() return type and implementation (Step 4)
- [ ] Delete sendRequest() method (Step 5)
- [ ] Delete sendRequestToConnection() method (Step 5)
- [ ] Delete sendRequestWithFailover() method (Step 5)
- [ ] Update connect() to set client intelligence (Step 6)
- [ ] Build and test
- [ ] Celebrate! 🎉

---

## Files to Edit

1. `src/operations/RemoteCache.cpp` - Main file, all changes here
2. No other files need changes (headers already updated)

**Estimated time**: 30-45 minutes

---

## Need Help?

Reference files:
- `src/operations/RemoteCache_NEW.cpp` - Has complete examples of all operations
- `include/hotrod/MultiplexedConnection.h` - See Response structure
- `docs/ASYNC_API_FINAL.md` - Overall design

**You've got this!** The pattern is clear and consistent. Just follow it for PUT, REMOVE, and PING, then update the helper methods. 🚀
