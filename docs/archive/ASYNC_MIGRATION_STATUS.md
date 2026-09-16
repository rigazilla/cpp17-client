# Async API Migration - Current Status

**Date**: 2026-07-01  
**Status**: Phase 1 Complete - Operations updated to async API

---

## What's Done ✅

### 1. Updated RemoteCache.h (API)

Changed all operation signatures to return futures:

```cpp
// Before
bool ping();
bool get(const ByteArray& key, ByteArray& value);
bool put(const ByteArray& key, const ByteArray& value, ...params..., ByteArray* previousValue);
bool remove(const ByteArray& key, ByteArray* previousValue);

// After
std::future<void> ping();
std::future<std::optional<ByteArray>> get(const ByteArray& key);
std::future<std::optional<ByteArray>> put(const ByteArray& key, const ByteArray& value, ...params...);
std::future<std::optional<ByteArray>> remove(const ByteArray& key);
```

**Changes:**
- No output parameters (value, previousValue)
- Return `std::optional<ByteArray>` instead of bool + output param
- Clean async signatures

### 2. Updated RemoteCache.cpp (Implementation)

**Temporary implementation approach:**
- Operations still execute synchronously (blocking)
- Create `std::promise` and immediately set value
- Return `promise.get_future()`
- Added TODO comments for MultiplexedConnection replacement

**Example (get):**
```cpp
std::future<std::optional<ByteArray>> RemoteCache::get(const ByteArray& key) {
    // ... existing synchronous code ...
    
    // Temporary: Create promise and set value immediately
    std::promise<std::optional<ByteArray>> promise;
    
    if (keyNotFound) {
        promise.set_value(std::nullopt);
    } else {
        ByteArray value = readFromSocket();
        promise.set_value(std::move(value));
    }
    
    return promise.get_future();
}
```

### 3. Updated Tests

**Updated**: `tests/integration/PingIntegrationTest.cpp`

```cpp
// Before
bool result = cache.ping();
EXPECT_TRUE(result);

// After
cache.ping().get();  // .get() blocks, throws on error
```

---

## What's Next 🔧

### Still TODO:

1. **Update remaining test files** (Task #4)
   - `tests/integration/GetIntegrationTest.cpp`
   - `tests/integration/PutIntegrationTest.cpp`
   - `tests/integration/RemoveIntegrationTest.cpp`
   - `tests/unit/*Test.cpp` files

2. **Implement MultiplexedConnection** (Task #2)
   - Read loop thread
   - Pending requests map
   - execute() method with body parsers
   - Topology update callbacks

3. **Add Connection helpers** (Task #3)
   - `receiveByteArray()`
   - `receiveVInt()`
   - `receiveVLong()`
   - `receiveString()`
   - `receiveMediaType()`

4. **Replace temporary promise implementation**
   - Update operations to use MultiplexedConnection::execute()
   - Add body parser lambdas
   - Remove synchronous code

---

## How to Update Tests

**Pattern for all tests:**

### PING
```cpp
// Before
bool result = cache.ping();
EXPECT_TRUE(result);

// After  
cache.ping().get();  // Throws on error, no need for EXPECT_TRUE
```

### GET
```cpp
// Before
ByteArray value;
bool found = cache.get(key, value);
EXPECT_TRUE(found);
EXPECT_EQ(expectedValue, value);

// After
auto value = cache.get(key).get();
ASSERT_TRUE(value.has_value());
EXPECT_EQ(expectedValue, *value);
```

### PUT
```cpp
// Before
ByteArray prevValue;
bool hadPrevious = cache.put(key, value, 0, 0, &prevValue);
if (hadPrevious) {
    EXPECT_EQ(expectedPrev, prevValue);
}

// After
auto prevValue = cache.put(key, value).get();
if (prevValue) {
    EXPECT_EQ(expectedPrev, *prevValue);
}
```

### REMOVE
```cpp
// Before
ByteArray prevValue;
bool existed = cache.remove(key, &prevValue);
EXPECT_TRUE(existed);
EXPECT_EQ(expectedValue, prevValue);

// After
auto prevValue = cache.remove(key).get();
ASSERT_TRUE(prevValue.has_value());
EXPECT_EQ(expectedValue, *prevValue);
```

---

## Build Status

**Current**: ✅ Library builds successfully  
**Tests**: ❌ Need updating for new API

```bash
# Build library (works)
cmake --build build --target hotrod-client

# Build tests (fails - need updates)
cmake --build build --target unit_tests
cmake --build build --target ping_integration_tests  # ✅ Updated
cmake --build build --target get_integration_tests   # ❌ Needs update
cmake --build build --target put_integration_tests   # ❌ Needs update
cmake --build build --target remove_integration_tests # ❌ Needs update
```

---

## Migration Benefits

Even with temporary synchronous implementation, we get:

✅ **Clean API** - no output parameters  
✅ **std::optional** - clear "not found" vs "error" semantics  
✅ **Future-ready** - API compatible with true async  
✅ **Type safety** - compiler catches API changes  

Once MultiplexedConnection is implemented:

✅ **True concurrency** - multiple operations in flight  
✅ **Better throughput** - request pipelining  
✅ **Efficient** - one socket per server  

---

## Files Changed

### Headers
- `include/hotrod/RemoteCache.h` - Updated signatures, added `<future>` and `<optional>`

### Implementation
- `src/operations/RemoteCache.cpp` - All 4 operations return futures (temporary impl)

### Tests  
- `tests/integration/PingIntegrationTest.cpp` - ✅ Updated to use `.get()`
- `tests/integration/GetIntegrationTest.cpp` - ❌ TODO
- `tests/integration/PutIntegrationTest.cpp` - ❌ TODO
- `tests/integration/RemoveIntegrationTest.cpp` - ❌ TODO
- `tests/unit/*.cpp` - ❌ TODO (many files)

---

## Next Steps

**Immediate (to make tests pass):**

1. Update `GetIntegrationTest.cpp`
2. Update `PutIntegrationTest.cpp`  
3. Update `RemoveIntegrationTest.cpp`
4. Update unit test files
5. Verify all tests pass

**Then (true async implementation):**

1. Implement `MultiplexedConnection` class
2. Add `Connection` helper methods
3. Replace temporary promise implementation
4. Add async-specific tests (concurrent operations)
5. Performance benchmarks

---

## Commands to Continue

```bash
# Update test files
vi tests/integration/GetIntegrationTest.cpp
vi tests/integration/PutIntegrationTest.cpp
vi tests/integration/RemoveIntegrationTest.cpp

# Build and run tests
cmake --build build
ctest --test-dir build --output-on-failure

# Or run specific test
./build/ping_integration_tests
```

---

**Ready to continue with test updates or move to MultiplexedConnection implementation?**
