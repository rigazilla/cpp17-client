# Quick Reference - MultiplexedConnection Migration

## Key Changes Summary

### Before (Blocking)
```cpp
std::future<std::optional<ByteArray>> RemoteCache::get(const ByteArray& key) {
    // Build complete request (header + body)
    RequestHeader header;
    header.messageId = nextMessageId();  // ❌ REMOVE
    ByteArray request;
    HeaderCodec::writeRequestHeader(request, header);  // ❌ REMOVE
    
    // Send and block
    auto [response, conn] = sendRequestWithFailover(request, key);  // ❌ REMOVE
    
    // Parse response
    ResponseHeader respHeader = HeaderCodec::readResponseHeader(response, offset);  // ❌ REMOVE
    
    // Read body
    ByteArray value = conn->receive(...);  // ❌ REMOVE
    
    // Return promise
    std::promise<std::optional<ByteArray>> promise;  // ❌ REMOVE
    promise.set_value(value);  // ❌ REMOVE
    return promise.get_future();  // ❌ REMOVE
}
```

### After (Async)
```cpp
std::future<std::optional<ByteArray>> RemoteCache::get(const ByteArray& key) {
    // Build request BODY only (no header)
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);  // ✅ KEEP

    // Body parser (runs in read thread)
    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == 0x00) return conn->receiveByteArray();
        return {};
    };

    // Execute
    MultiplexedConnection* conn = selectServerForKey(key);
    auto responseFuture = conn->execute(requestBody, 0x03, 0x04, bodyParser, cacheName_);

    // Transform
    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable -> std::optional<ByteArray> {
            Response resp = responseFuture.get();
            if (resp.error) std::rethrow_exception(resp.error);
            if (resp.status == 0x00) return std::move(resp.body);
            return std::nullopt;
        });
}
```

---

## Search & Replace Patterns

### 1. Remove Message ID Generation
**Search**: `header.messageId = nextMessageId();`  
**Action**: Delete this line (MultiplexedConnection generates IDs)

### 2. Remove Header Encoding
**Search**: `HeaderCodec::writeRequestHeader(request, header);`  
**Action**: Delete (MultiplexedConnection builds headers)

### 3. Replace Connection Type
**Search**: `Connection*`  
**Replace**: `MultiplexedConnection*`

### 4. Remove Old Send Methods
**Search**: `sendRequestWithFailover`  
**Action**: Delete function and all calls

---

## Copy-Paste Templates

### Template: Operation with Key
```cpp
std::future<std::optional<ByteArray>> RemoteCache::OPERATION(const ByteArray& key, ...) {
    ByteArray requestBody;
    Codec::writeByteArray(requestBody, key);
    // ... add other request fields ...

    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        if (status == SUCCESS_CODE) return conn->receiveByteArray();
        return {};
    };

    MultiplexedConnection* conn = selectServerForKey(key);
    auto responseFuture = conn->execute(requestBody, REQ_OP, RESP_OP, bodyParser, cacheName_);

    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable -> std::optional<ByteArray> {
            Response resp = responseFuture.get();
            if (resp.error) std::rethrow_exception(resp.error);
            if (resp.status == SUCCESS_CODE) return std::move(resp.body);
            return std::nullopt;
        });
}
```

### Template: Operation Returning void (PING)
```cpp
std::future<void> RemoteCache::ping() {
    ByteArray requestBody;  // Empty

    auto bodyParser = [](uint8_t status, Connection* conn) -> ByteArray {
        // Read and discard response body
        return {};
    };

    auto responseFuture = connection_->execute(requestBody, 0x17, 0x18, bodyParser, cacheName_);

    return std::async(std::launch::deferred,
        [responseFuture = std::move(responseFuture)]() mutable {
            Response resp = responseFuture.get();
            if (resp.error) std::rethrow_exception(resp.error);
        });
}
```

---

## Common Opcodes

| Operation | Request | Response |
|-----------|---------|----------|
| PING      | 0x17    | 0x18     |
| GET       | 0x03    | 0x04     |
| PUT       | 0x01    | 0x02     |
| REMOVE    | 0x0B    | 0x0C     |

---

## Status Codes

| Code | Meaning |
|------|---------|
| 0x00 | SUCCESS (no previous value) |
| 0x01 | KEY_DOES_NOT_EXIST |
| 0x02 | NOT_FOUND |
| 0x03 | SUCCESS_WITH_PREVIOUS |

---

## What to Keep vs Remove

### ✅ KEEP
- Request body encoding (`Codec::writeByteArray`, `Codec::writeVLong`, etc.)
- Time units calculation for PUT
- Key/value encoding
- selectServerForKey() logic (just change return type)
- getConnectionForServer() logic (just change return type)

### ❌ REMOVE
- `nextMessageId()` calls
- `HeaderCodec::writeRequestHeader()` calls
- `sendRequest()` function
- `sendRequestToConnection()` function
- `sendRequestWithFailover()` function
- `std::promise` / `promise.set_value()` patterns
- Manual response header parsing
- Manual socket reading in operation methods

---

## Files Reference

**Already Done**:
- `include/hotrod/MultiplexedConnection.h` ✅
- `src/transport/MultiplexedConnection.cpp` ✅
- `src/transport/Connection.cpp` (helper methods) ✅
- `include/hotrod/RemoteCache.h` ✅
- GET operation ✅

**You Need to Update**:
- `src/operations/RemoteCache.cpp` - PUT, REMOVE, PING operations
- `src/operations/RemoteCache.cpp` - Helper methods (selectServerForKey, getConnectionForServer)

---

## Minimal Test

After your changes, test with:

```bash
# Build
cmake --build build

# Quick test
./build/ping_integration_tests

# If PING works, the pattern is correct for all operations!
```

---

**Total Lines to Change**: ~200 lines in 1 file  
**Time Estimate**: 30-45 minutes  
**Difficulty**: Low (repetitive pattern)
