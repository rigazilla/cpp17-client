# Test Update Progress

## Status: Partially Complete

### Done ✅
- **PingIntegrationTest.cpp** - Fully updated and compiles
- **GetIntegrationTest.cpp** - Fully updated and compiles  
- **RemoteCache API** - All operations return futures

### In Progress 🔧
- **PutIntegrationTest.cpp** - Partially updated, has compile errors
- **RemoveIntegrationTest.cpp** - Partially updated, not tested
- **HashAwareRoutingIntegrationTest.cpp** - Partially updated, not tested

### Not Started ⏳
- **Unit tests** (tests/unit/*.cpp) - Many files

---

## Common Patterns for Manual Updates

### Pattern 1: Simple PING
```cpp
// Before
bool result = cache.ping();
EXPECT_TRUE(result);

// After
cache.ping().get();  // Throws on error
```

### Pattern 2: GET with check
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

### Pattern 3: GET expecting not found
```cpp
// Before
ByteArray value;
bool found = cache.get(key, value);
EXPECT_FALSE(found);

// After
auto value = cache.get(key).get();
EXPECT_FALSE(value.has_value());
```

### Pattern 4: PUT without checking previous
```cpp
// Before
cache.put(key, value);

// After
cache.put(key, value).get();
```

### Pattern 5: PUT checking if previous existed
```cpp
// Before
ByteArray prevValue;
bool hadPrevious = cache.put(key, value, 0, 0, &prevValue);
EXPECT_FALSE(hadPrevious);  // or EXPECT_TRUE

// After
auto prevValue = cache.put(key, value).get();
EXPECT_FALSE(prevValue.has_value());  // or ASSERT_TRUE
```

### Pattern 6: REMOVE with previous value
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

## Recommended Approach

**Option A: Finish manually** (tedious but reliable)
1. Open each test file
2. Find-replace using patterns above
3. Fix compilation errors one by one
4. Test that each file compiles

**Option B: Regenerate tests** (faster but loses custom test logic)
1. Keep test structure
2. Rewrite test bodies with new API
3. Ensure same test coverage

**Option C: Use better tooling**
1. Use `clang-tidy` with custom checks
2. Or write AST-based refactoring tool
3. More complex upfront, perfect results

---

## Next Steps

1. **Fix PutIntegrationTest.cpp** manually
   - Lines with errors: 73, 76, 77, 160, 194
   - Fix variable name conflicts (prevValue vs prevValue1/prevValue2)
   - Fix optional dereferencing (value vs *value)

2. **Fix RemoveIntegrationTest.cpp**
   - Similar patterns to PUT

3. **Fix HashAwareRoutingIntegrationTest.cpp**
   - Mixed GET/PUT/PING calls

4. **Update unit tests**
   - Many smaller files
   - Mostly operation-specific logic

---

## Quick Fix Commands

For someone to continue:

```bash
# Check what still needs fixing
cmake --build build 2>&1 | grep "error:" | grep -o "tests/.*\.cpp" | sort -u

# Fix a specific file
vi tests/integration/PutIntegrationTest.cpp
# Apply patterns above manually

# Test compilation
cmake --build build --target put_integration_tests

# When all integration tests pass
ctest --test-dir build -R Integration --output-on-failure
```

---

## Estimated Time Remaining

- Put/Remove/HashAware integration tests: **30-45 minutes** (manual fixes)
- Unit tests: **1-2 hours** (many files, but smaller)
- **Total: 2-3 hours** of careful manual work

OR

- Rewrite test helpers with new API: **1 hour**
- Regenerate all tests: **30 minutes**
- **Total: 1.5 hours** but loses any custom test logic
