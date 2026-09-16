# Async API - Final Design

**Decision**: Breaking change - async-only API (no `Async` suffixes, no sync wrappers).

This is a **prototype project**, so we can make clean architectural decisions now.

---

## Final API

```cpp
class RemoteCache {
public:
    RemoteCache(const std::string& host, uint16_t port = 11222);
    RemoteCache(const std::string& host, uint16_t port, const std::string& cacheName);
    ~RemoteCache();
    
    void connect();
    void disconnect();
    bool isConnected() const;
    
    // All operations return futures (async by default)
    std::future<void> ping();
    std::future<std::optional<ByteArray>> get(const ByteArray& key);
    std::future<std::optional<ByteArray>> put(const ByteArray& key, 
                                                const ByteArray& value,
                                                uint64_t lifespan = 0, 
                                                uint64_t maxIdle = 0);
    std::future<std::optional<ByteArray>> remove(const ByteArray& key);
    
    // Configuration
    void setCacheName(const std::string& name);
    const std::string& getCacheName() const;
    void setClientIntelligence(ClientIntelligence intelligence);
    ClientIntelligence getClientIntelligence() const;
    
    // Topology info
    const TopologyInfo& getTopology() const;
    VInt getTopologyId() const;
};
```

**No sync wrappers, no `Async` suffixes.**

---

## Usage Examples

### Simple blocking get

```cpp
RemoteCache cache("localhost", 11222);
cache.connect();

// Block and get result
if (auto value = cache.get(key).get()) {
    std::cout << "Value: " << *value << "\n";
} else {
    std::cout << "Not found\n";
}
```

### Async with work in between

```cpp
// Start GET
auto future = cache.get(key);

// Do other work while GET is in flight
processOtherData();
computeSomething();

// Now wait for result
if (auto value = future.get()) {
    std::cout << "Value: " << *value << "\n";
}
```

### Parallel operations

```cpp
// Issue multiple operations concurrently
auto f1 = cache.get(key1);
auto f2 = cache.get(key2);
auto f3 = cache.get(key3);

// Wait for all
auto v1 = f1.get();
auto v2 = f2.get();
auto v3 = f3.get();

// Or wait for first to complete
std::vector<std::future<std::optional<ByteArray>>> futures;
futures.push_back(cache.get(key1));
futures.push_back(cache.get(key2));

// Custom wait_any implementation (C++17 doesn't have std::when_any)
while (true) {
    for (auto& f : futures) {
        if (f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto value = f.get();
            // Process first result
            break;
        }
    }
}
```

### Error handling

```cpp
try {
    auto value = cache.get(key).get();
    if (value) {
        process(*value);
    }
} catch (const ConnectionError& e) {
    std::cerr << "Connection failed: " << e.what() << "\n";
} catch (const ServerError& e) {
    std::cerr << "Server error: " << e.what() << "\n";
}
```

### Fire and forget (PUT without waiting)

```cpp
// Start PUT, don't wait
auto future = cache.put(key, value);

// Do other work...
// future will complete in background

// Later, ensure it succeeded (throws if error)
future.get();
```

### Batch operations

```cpp
std::vector<std::future<std::optional<ByteArray>>> futures;

// Issue all GETs
for (const auto& key : keys) {
    futures.push_back(cache.get(key));
}

// Collect results
std::vector<ByteArray> values;
for (auto& future : futures) {
    if (auto value = future.get()) {
        values.push_back(std::move(*value));
    }
}
```

---

## Implementation Details

### Return Types

#### ping()
```cpp
std::future<void> ping();
```
- Throws exception if ping fails
- Returns void (no data)

#### get()
```cpp
std::future<std::optional<ByteArray>> get(const ByteArray& key);
```
- Returns `std::optional<ByteArray>`
- `std::nullopt` = key not found
- Throws exception on communication errors

#### put()
```cpp
std::future<std::optional<ByteArray>> put(const ByteArray& key, 
                                           const ByteArray& value,
                                           uint64_t lifespan = 0,
                                           uint64_t maxIdle = 0);
```
- Returns `std::optional<ByteArray>` (previous value if existed)
- `std::nullopt` = no previous value (new insert)
- Throws exception on errors

#### remove()
```cpp
std::future<std::optional<ByteArray>> remove(const ByteArray& key);
```
- Returns `std::optional<ByteArray>` (previous value if key existed)
- `std::nullopt` = key didn't exist
- Throws exception on errors

---

## Implementation Plan

### Step 1: MultiplexedConnection class

**File**: `include/hotrod/MultiplexedConnection.h`

```cpp
#pragma once

#include "Types.h"
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <future>
#include <atomic>
#include <memory>
#include <functional>

namespace hotrod {

struct Response {
    uint8_t status;
    ByteArray body;
    std::exception_ptr error;
    std::optional<TopologyInfo> topologyUpdate;
};

struct PendingRequest {
    std::promise<Response> promise;
    std::function<ByteArray(uint8_t status, Connection*)> bodyParser;
    uint8_t expectedOpcode;
    std::string cacheName;
};

class MultiplexedConnection {
public:
    using TopologyCallback = std::function<void(const TopologyInfo&, const std::string& cacheName)>;
    
    MultiplexedConnection(const std::string& host, 
                          uint16_t port,
                          TopologyCallback onTopologyUpdate = nullptr);
    ~MultiplexedConnection();
    
    void connect();
    void close();
    bool isConnected() const;
    
    std::future<Response> execute(
        const ByteArray& requestBody,
        uint8_t requestOpcode,
        uint8_t expectedResponseOpcode,
        std::function<ByteArray(uint8_t status, Connection*)> bodyParser,
        const std::string& cacheName = ""
    );
    
private:
    std::string host_;
    uint16_t port_;
    int socket_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopReadLoop_{false};
    
    std::atomic<uint64_t> nextMessageId_{0};
    
    std::mutex pendingMutex_;
    std::map<uint64_t, std::unique_ptr<PendingRequest>> pending_;
    
    std::mutex writeMutex_;
    std::unique_ptr<std::thread> readThread_;
    
    TopologyCallback onTopologyUpdate_;
    ClientIntelligence clientIntelligence_;
    std::atomic<int32_t> topologyId_{0};
    
    void readLoop();
    void sendData(const ByteArray& data);
    ByteArray receiveData(size_t length);
    void closeAllPending(const std::string& errorMsg);
    ByteArray buildRequest(uint64_t messageId, 
                           uint8_t opcode, 
                           const ByteArray& body,
                           const std::string& cacheName);
};

} // namespace hotrod
```

### Step 2: Connection Helper Methods

**File**: `include/hotrod/Connection.h` (additions)

```cpp
class Connection {
public:
    // Existing
    ByteArray receive(size_t length);
    void send(const ByteArray& data);
    
    // New helpers for body parsers
    VInt receiveVInt();
    VLong receiveVLong();
    ByteArray receiveByteArray();  // lp_bytes (vInt length + bytes)
    std::string receiveString();   // lp_string
    ByteArray receiveMediaType();  // media_type structure
};
```

### Step 3: Update RemoteCache

**File**: `include/hotrod/RemoteCache.h`

```cpp
class RemoteCache {
public:
    RemoteCache(const std::string& host, uint16_t port = 11222);
    RemoteCache(const std::string& host, uint16_t port, const std::string& cacheName);
    ~RemoteCache();
    
    void connect();
    void disconnect();
    bool isConnected() const;
    
    // Async operations (no Async suffix)
    std::future<void> ping();
    std::future<std::optional<ByteArray>> get(const ByteArray& key);
    std::future<std::optional<ByteArray>> put(const ByteArray& key, 
                                                const ByteArray& value,
                                                uint64_t lifespan = 0,
                                                uint64_t maxIdle = 0);
    std::future<std::optional<ByteArray>> remove(const ByteArray& key);
    
    // Configuration
    void setCacheName(const std::string& name) { cacheName_ = name; }
    const std::string& getCacheName() const { return cacheName_; }
    void setClientIntelligence(ClientIntelligence intelligence);
    ClientIntelligence getClientIntelligence() const;
    const TopologyInfo& getTopology() const { return topology_; }
    VInt getTopologyId() const { return topology_.getTopologyId(); }

private:
    std::string host_;
    uint16_t port_;
    std::string cacheName_;
    
    // Connection pool: one MultiplexedConnection per server
    std::map<std::string, std::unique_ptr<MultiplexedConnection>> connectionPool_;
    
    ClientIntelligence clientIntelligence_;
    TopologyInfo topology_;
    ConsistentHash consistentHash_;
    
    MultiplexedConnection* selectServerForKey(const ByteArray& key);
    MultiplexedConnection* getConnectionForServer(const ServerInfo& server);
    void handleTopologyUpdate(const TopologyInfo& topo, const std::string& cacheName);
};
```

### Step 4: Implement Operations

**Example: get()**

```cpp
std::future<std::optional<ByteArray>> RemoteCache::get(const ByteArray& key) {
    // Build request body
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);
    
    // Define body parser
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x01 || status == 0x02) {
            return {};  // Not found
        }
        if (status != 0x00) {
            throw std::runtime_error("GET failed with status: " + std::to_string(status));
        }
        return conn->receiveByteArray();
    };
    
    // Select connection (hash-aware routing)
    MultiplexedConnection* conn = selectServerForKey(key);
    
    // Execute and get Response future
    auto responseFuture = conn->execute(requestBody, 0x03, 0x04, bodyParser, cacheName_);
    
    // Transform Response → optional<ByteArray>
    return std::async(std::launch::deferred, 
        [responseFuture = std::move(responseFuture)]() mutable -> std::optional<ByteArray> {
            Response resp = responseFuture.get();
            
            if (resp.error) {
                std::rethrow_exception(resp.error);
            }
            
            if (resp.status == 0x00) {
                return std::optional<ByteArray>(std::move(resp.body));
            }
            
            return std::nullopt;
        });
}
```

---

## Migration Checklist

### Core Infrastructure
- [ ] Implement MultiplexedConnection class
  - [ ] Constructor, destructor
  - [ ] connect(), close()
  - [ ] execute() method
  - [ ] readLoop() thread
  - [ ] Pending requests map
  - [ ] Write mutex
  - [ ] buildRequest() helper

### Connection Helpers
- [ ] receiveVInt()
- [ ] receiveVLong()
- [ ] receiveByteArray()
- [ ] receiveString()
- [ ] receiveMediaType()

### RemoteCache Operations
- [ ] ping() → std::future<void>
- [ ] get() → std::future<std::optional<ByteArray>>
- [ ] put() → std::future<std::optional<ByteArray>>
- [ ] remove() → std::future<std::optional<ByteArray>>

### Connection Pool
- [ ] Update connectionPool_ to use MultiplexedConnection
- [ ] Update selectServerForKey()
- [ ] Update getConnectionForServer()
- [ ] Topology update callback

### Tests
- [ ] Update all unit tests (change API calls)
- [ ] Update all integration tests (change API calls)
- [ ] Add async-specific tests (parallel operations)
- [ ] Add stress tests (1000s of concurrent operations)

### Documentation
- [ ] Update README with new async API
- [ ] Update quickstart example
- [ ] Add async usage examples
- [ ] Update PROGRESS.md

---

## Breaking Changes Summary

### Before
```cpp
ByteArray value;
if (cache.get(key, value)) {
    process(value);
}

cache.put(key, value);

ByteArray prevValue;
if (cache.remove(key, &prevValue)) {
    process(prevValue);
}
```

### After
```cpp
if (auto value = cache.get(key).get()) {
    process(*value);
}

cache.put(key, value).get();

if (auto prevValue = cache.remove(key).get()) {
    process(*prevValue);
}
```

**Migration effort**: ~5 minutes per file (simple find-replace)

---

## Implementation Order

1. **Phase 1: MultiplexedConnection** (core infrastructure)
   - Implement class with execute(), readLoop()
   - Unit test with mock body parsers
   
2. **Phase 2: Connection helpers**
   - receiveByteArray(), receiveVInt(), etc.
   - Unit test each helper
   
3. **Phase 3: RemoteCache operations**
   - Implement ping(), get(), put(), remove()
   - Update to return futures
   
4. **Phase 4: Update tests**
   - Fix all existing tests to use .get()
   - Should all pass
   
5. **Phase 5: Add async tests**
   - Concurrent operations
   - Performance benchmarks

---

## Ready to Implement?

**Questions:**
1. Start with Phase 1 (MultiplexedConnection)?
2. Want to review any part of the design first?
3. Any concerns about the breaking changes?

The plan is clear, and the API is clean. Ready when you are!
