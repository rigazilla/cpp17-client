# Async API Migration Plan

**Goal**: Convert all operations to return `std::future` instead of blocking.

## Current API (Blocking)

```cpp
class RemoteCache {
public:
    bool ping();
    bool get(const ByteArray& key, ByteArray& value);
    bool put(const ByteArray& key, const ByteArray& value, 
             uint64_t lifespan = 0, uint64_t maxIdle = 0,
             ByteArray* previousValue = nullptr);
    bool remove(const ByteArray& key, ByteArray* previousValue = nullptr);
};

// Usage
RemoteCache cache("localhost", 11222);
cache.connect();

ByteArray value;
if (cache.get(key, value)) {  // Blocks here
    std::cout << "Found: " << value << "\n";
}
```

**Problems:**
- ❌ Blocks calling thread
- ❌ Can't do multiple operations concurrently from same thread
- ❌ Can't compose operations (chaining, wait-any, etc.)

---

## New API (Async)

```cpp
class RemoteCache {
public:
    // Async API (returns future)
    std::future<void> pingAsync();
    std::future<std::optional<ByteArray>> getAsync(const ByteArray& key);
    std::future<std::optional<ByteArray>> putAsync(const ByteArray& key, const ByteArray& value,
                                                     uint64_t lifespan = 0, uint64_t maxIdle = 0);
    std::future<std::optional<ByteArray>> removeAsync(const ByteArray& key);
    
    // Synchronous wrappers (backward compatible)
    void ping();
    bool get(const ByteArray& key, ByteArray& value);
    bool put(const ByteArray& key, const ByteArray& value,
             uint64_t lifespan = 0, uint64_t maxIdle = 0,
             ByteArray* previousValue = nullptr);
    bool remove(const ByteArray& key, ByteArray* previousValue = nullptr);
};

// Usage - Async
RemoteCache cache("localhost", 11222);
cache.connect();

// Start multiple operations in parallel
auto f1 = cache.getAsync(key1);
auto f2 = cache.getAsync(key2);
auto f3 = cache.putAsync(key3, value3);

// Wait and process results
if (auto value = f1.get()) {
    std::cout << "key1 found: " << *value << "\n";
}
if (auto value = f2.get()) {
    std::cout << "key2 found: " << *value << "\n";
}
f3.get();  // Wait for PUT to complete

// Usage - Sync (backward compatible)
ByteArray value;
if (cache.get(key, value)) {  // Still works!
    std::cout << "Found: " << value << "\n";
}
```

**Benefits:**
- ✅ Non-blocking (can issue multiple operations)
- ✅ Composable (use std::async, std::when_any, etc.)
- ✅ Backward compatible (sync API wraps async)
- ✅ Cleaner API (no output parameters for async)

---

## API Design Decisions

### Return Types

**Option A: std::optional<ByteArray>** (Recommended)
```cpp
std::future<std::optional<ByteArray>> getAsync(const ByteArray& key);

// Usage
auto future = cache.getAsync(key);
std::optional<ByteArray> result = future.get();
if (result) {
    std::cout << "Value: " << *result << "\n";
} else {
    std::cout << "Key not found\n";
}
```
**Pros**: Clear distinction between "not found" vs "error" (exception)  
**Cons**: C++17 needed for std::optional (already using C++17!)

**Option B: ByteArray with empty = not found**
```cpp
std::future<ByteArray> getAsync(const ByteArray& key);

// Usage
ByteArray value = future.get();
if (value.empty()) {
    std::cout << "Not found or empty value\n";  // Ambiguous!
}
```
**Pros**: Simpler return type  
**Cons**: Can't distinguish empty value from not found

**Option C: Pair<bool, ByteArray>**
```cpp
std::future<std::pair<bool, ByteArray>> getAsync(const ByteArray& key);

// Usage
auto [found, value] = future.get();
if (found) { ... }
```
**Pros**: Clear boolean  
**Cons**: More verbose than optional

**Decision: Option A (std::optional)** - clearest semantics

---

### Error Handling

**Async operations signal errors via exceptions in future:**

```cpp
try {
    auto value = cache.getAsync(key).get();
    // Use value
} catch (const ConnectionError& e) {
    std::cerr << "Connection failed: " << e.what() << "\n";
} catch (const ServerError& e) {
    std::cerr << "Server error: " << e.what() << "\n";
}
```

**Or check for exceptions without throwing:**
```cpp
auto future = cache.getAsync(key);
// ... do other work ...

try {
    auto value = future.get();
} catch (...) {
    // Handle error
}
```

---

## Detailed API Specification

### PING

```cpp
// Async: Returns void future (just checks if ping succeeded)
std::future<void> RemoteCache::pingAsync() {
    ByteArray requestBody;  // Empty
    
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        conn->receiveMediaType();  // key_media_type
        conn->receiveMediaType();  // value_media_type
        conn->receive(1);          // server_version
        VInt opCount = conn->receiveVInt();
        for (VInt i = 0; i < opCount; i++) {
            conn->receive(2);  // uint16 opcode
        }
        
        if (status != 0x00) {
            throw std::runtime_error("PING failed with status: " + std::to_string(status));
        }
        
        return {};  // No data returned
    };
    
    Connection* conn = selectServerForKey({});
    auto responseFuture = conn->execute(requestBody, 0x17, 0x18, bodyParser, cacheName_);
    
    // Transform Response future to void future
    return std::async(std::launch::deferred, [responseFuture = std::move(responseFuture)]() mutable {
        Response resp = responseFuture.get();
        if (resp.error) {
            std::rethrow_exception(resp.error);
        }
        // Success - return void
    });
}

// Sync wrapper
void RemoteCache::ping() {
    pingAsync().get();  // Block and wait
}
```

### GET

```cpp
// Async: Returns optional<ByteArray>
std::future<std::optional<ByteArray>> RemoteCache::getAsync(const ByteArray& key) {
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);
    
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x01 || status == 0x02) {
            // Not found - return empty (we'll convert to nullopt)
            return {};
        }
        if (status != 0x00) {
            throw std::runtime_error("GET failed with status: " + std::to_string(status));
        }
        
        return conn->receiveByteArray();
    };
    
    Connection* conn = selectServerForKey(key);
    auto responseFuture = conn->execute(requestBody, 0x03, 0x04, bodyParser, cacheName_);
    
    // Transform Response to optional<ByteArray>
    return std::async(std::launch::deferred, [responseFuture = std::move(responseFuture)]() mutable {
        Response resp = responseFuture.get();
        
        if (resp.error) {
            std::rethrow_exception(resp.error);
        }
        
        if (resp.status == 0x00) {
            return std::optional<ByteArray>(std::move(resp.body));
        }
        
        return std::optional<ByteArray>(std::nullopt);  // Not found
    });
}

// Sync wrapper (backward compatible)
bool RemoteCache::get(const ByteArray& key, ByteArray& value) {
    auto result = getAsync(key).get();
    if (result) {
        value = std::move(*result);
        return true;
    }
    return false;
}
```

### PUT

```cpp
// Async: Returns optional<ByteArray> (previous value if existed)
std::future<std::optional<ByteArray>> RemoteCache::putAsync(
    const ByteArray& key,
    const ByteArray& value,
    uint64_t lifespan,
    uint64_t maxIdle)
{
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);
    
    // Time units
    uint8_t timeUnits = 0;
    if (lifespan == 0) {
        timeUnits |= (0x07 << 4);
    } else {
        timeUnits |= (0x00 << 4);
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
    
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x03) {
            // SUCCESS_WITH_PREVIOUS
            return conn->receiveByteArray();
        }
        return {};
    };
    
    Connection* conn = selectServerForKey(key);
    auto responseFuture = conn->execute(requestBody, 0x01, 0x02, bodyParser, cacheName_);
    
    return std::async(std::launch::deferred, [responseFuture = std::move(responseFuture)]() mutable {
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
        
        return std::optional<ByteArray>(std::nullopt);
    });
}

// Sync wrapper
bool RemoteCache::put(const ByteArray& key, const ByteArray& value,
                      uint64_t lifespan, uint64_t maxIdle,
                      ByteArray* previousValue)
{
    auto result = putAsync(key, value, lifespan, maxIdle).get();
    
    if (result && previousValue) {
        *previousValue = std::move(*result);
        return true;
    }
    
    return !result;  // Returns true if no previous value (new insert)
}
```

### REMOVE

```cpp
// Async: Returns optional<ByteArray> (previous value if existed)
std::future<std::optional<ByteArray>> RemoteCache::removeAsync(const ByteArray& key) {
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);
    
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x03) {
            return conn->receiveByteArray();
        }
        return {};
    };
    
    Connection* conn = selectServerForKey(key);
    auto responseFuture = conn->execute(requestBody, 0x0B, 0x0C, bodyParser, cacheName_);
    
    return std::async(std::launch::deferred, [responseFuture = std::move(responseFuture)]() mutable {
        Response resp = responseFuture.get();
        
        if (resp.error) {
            std::rethrow_exception(resp.error);
        }
        
        if (resp.status == 0x00 || resp.status == 0x03) {
            // Key existed and was removed
            if (resp.status == 0x03 && !resp.body.empty()) {
                return std::optional<ByteArray>(std::move(resp.body));
            }
            return std::optional<ByteArray>(std::nullopt);
        }
        
        // Key didn't exist (0x01, 0x02)
        return std::optional<ByteArray>(std::nullopt);
    });
}

// Sync wrapper
bool RemoteCache::remove(const ByteArray& key, ByteArray* previousValue) {
    auto result = removeAsync(key).get();
    
    if (result && previousValue) {
        *previousValue = std::move(*result);
        return true;
    }
    
    return !result;  // Returns true if key existed
}
```

---

## Async Usage Examples

### Example 1: Parallel GET operations

```cpp
RemoteCache cache("localhost", 11222);
cache.connect();

// Issue 100 GETs in parallel
std::vector<std::future<std::optional<ByteArray>>> futures;
for (int i = 0; i < 100; i++) {
    ByteArray key = makeKey(i);
    futures.push_back(cache.getAsync(key));
}

// Wait for all and process results
for (int i = 0; i < 100; i++) {
    if (auto value = futures[i].get()) {
        std::cout << "Key " << i << ": " << *value << "\n";
    } else {
        std::cout << "Key " << i << ": not found\n";
    }
}
```

### Example 2: Fire-and-forget PUT

```cpp
// Start PUT but don't wait
auto future = cache.putAsync(key, value);

// Do other work...
doSomethingElse();

// Later: ensure PUT completed
future.get();  // Will throw if PUT failed
```

### Example 3: Timeout handling

```cpp
auto future = cache.getAsync(key);

// Wait with timeout
if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
    auto value = future.get();
    // Use value
} else {
    std::cerr << "GET timed out!\n";
    // Note: future is still pending, will complete eventually
}
```

### Example 4: Conditional operations

```cpp
// Try to GET, if not found, PUT
auto getFuture = cache.getAsync(key);
auto value = getFuture.get();

if (!value) {
    // Not found, insert it
    cache.putAsync(key, defaultValue).get();
}
```

### Example 5: Batch operations with error handling

```cpp
std::vector<std::future<std::optional<ByteArray>>> futures;

// Issue batch
for (const auto& key : keys) {
    futures.push_back(cache.getAsync(key));
}

// Wait and handle errors
int successCount = 0;
int notFoundCount = 0;
int errorCount = 0;

for (auto& future : futures) {
    try {
        if (auto value = future.get()) {
            successCount++;
        } else {
            notFoundCount++;
        }
    } catch (const std::exception& e) {
        errorCount++;
        std::cerr << "Error: " << e.what() << "\n";
    }
}

std::cout << "Success: " << successCount 
          << ", Not found: " << notFoundCount
          << ", Errors: " << errorCount << "\n";
```

---

## Migration Strategy

### Phase 1: Implement Async Methods (New Code)

1. Add `*Async()` methods to RemoteCache
2. Implement using MultiplexedConnection::execute()
3. Return transformed futures (Response → std::optional<ByteArray>)
4. Unit test async methods

### Phase 2: Wrap Sync Methods (Existing API)

1. Change existing sync methods to call async + .get()
2. No API changes for users
3. Run existing integration tests (should all pass)

### Phase 3: Update Examples and Documentation

1. Add async examples to README
2. Document performance benefits
3. Add async integration tests

### Phase 4: Deprecate Sync (Optional, Future)

1. Mark sync methods as [[deprecated]]
2. Guide users to async API
3. Eventually remove sync wrappers (breaking change)

---

## Implementation Checklist

### MultiplexedConnection

- [ ] Implement execute() returning std::future<Response>
- [ ] Implement read loop thread
- [ ] Implement pending requests map
- [ ] Add topology update callback support

### Helper Methods (Connection class)

- [ ] `ByteArray receiveByteArray()`
- [ ] `VInt receiveVInt()`
- [ ] `VLong receiveVLong()`
- [ ] `std::string receiveString()`
- [ ] `ByteArray receiveMediaType()`

### RemoteCache Async API

- [ ] `std::future<void> pingAsync()`
- [ ] `std::future<std::optional<ByteArray>> getAsync(const ByteArray& key)`
- [ ] `std::future<std::optional<ByteArray>> putAsync(...)`
- [ ] `std::future<std::optional<ByteArray>> removeAsync(const ByteArray& key)`

### RemoteCache Sync Wrappers

- [ ] `void ping()` → calls `pingAsync().get()`
- [ ] `bool get(...)` → calls `getAsync().get()`
- [ ] `bool put(...)` → calls `putAsync().get()`
- [ ] `bool remove(...)` → calls `removeAsync().get()`

### Tests

- [ ] Unit tests: Async methods with mocked connection
- [ ] Integration tests: Concurrent async operations
- [ ] Integration tests: Existing sync API still works
- [ ] Performance tests: 1 thread sync vs N threads async

---

## Questions to Resolve

1. **std::launch policy**: Use `std::launch::deferred` or `std::launch::async`?
   - **deferred**: Transformation runs when .get() is called (lazy)
   - **async**: Transformation runs in separate thread (eager)
   - **Recommendation**: deferred (simpler, no extra threads)

2. **Future transformation**: Use std::async or custom promise?
   - **std::async**: Simple, standard library
   - **Custom promise**: More control, no extra overhead
   - **Recommendation**: std::async with deferred (simple and standard)

3. **Backward compatibility**: Keep sync API forever or deprecate?
   - **Keep forever**: Easy migration, no breaking changes
   - **Deprecate**: Force users to async (better performance)
   - **Recommendation**: Keep sync wrappers (low cost, high value)

4. **Connection pooling**: One MultiplexedConnection per server?
   - **Yes**: Simpler, matches Go client
   - **Recommendation**: Keep existing pool structure

---

## Performance Expectations

**Current (blocking, one socket per server):**
- 1 thread: ~1000 ops/sec (limited by round-trip time)
- 10 threads: ~10,000 ops/sec (10 connections × 1000 ops/sec)

**With multiplexing (async, one socket per server):**
- 1 thread issuing async: ~5000 ops/sec (pipeline depth limited by TCP window)
- 10 threads issuing async: ~50,000 ops/sec (higher pipeline depth)

**Benefits:**
- ✅ Lower memory (fewer connections)
- ✅ Higher throughput (request pipelining)
- ✅ Better latency (no connection pool contention)

---

## Summary

**Async API = Simple + Powerful:**

```cpp
// Before (blocking)
ByteArray v1;
cache.get(k1, v1);  // Block 1ms
ByteArray v2;
cache.get(k2, v2);  // Block 1ms
// Total: 2ms

// After (async)
auto f1 = cache.getAsync(k1);  // Issue immediately
auto f2 = cache.getAsync(k2);  // Issue immediately
auto v1 = f1.get();  // Wait ~1ms for both
auto v2 = f2.get();  // Already done
// Total: ~1ms
```

**Ready to implement?**
