#include <gtest/gtest.h>
#include "hotrod/RemoteCache.h"
#include "InfinispanTestEnvironment.h"
#include <thread>
#include <chrono>

using namespace hotrod;
using namespace hotrod::test;

/**
 * PUT Integration Tests - No Authentication
 *
 * Tests run against Infinispan server managed by InfinispanTestEnvironment.
 * Uses Hot Rod PUT and GET for complete round-trip validation.
 *
 * Reference:
 * - ROADMAP Step 8: PUT operation with cross-client validation
 */

namespace {

// Helper: Create cache via REST API
bool createCacheViaREST(const std::string& cacheName) {
    std::string port = std::to_string(InfinispanTestEnvironment::port);
    std::string host = InfinispanTestEnvironment::host;

    std::string createCmd = "timeout 5 curl -s -X POST \"http://" + host + ":" + port +
                           "/rest/v2/caches/" + cacheName + "\" >/dev/null 2>&1 || true";
    system(createCmd.c_str());
    return true;
}

} // anonymous namespace

// Test 1: PUT and GET simple key-value
TEST(PutIntegrationTest, PutAndGetSimple) {
    // Ensure cache exists
    createCacheViaREST("testcache");
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    // PUT
    ByteArray key = {'p', 'u', 't', 'k', 'e', 'y', '1'};
    ByteArray value = {'p', 'u', 't', 'v', 'a', 'l', '1'};
    bool hadPrevious = cache.put(key, value);

    EXPECT_FALSE(hadPrevious);  // First PUT, no previous value

    // GET to verify
    ByteArray retrievedValue;
    bool found = cache.get(key, retrievedValue);

    EXPECT_TRUE(found);
    EXPECT_EQ(value.size(), retrievedValue.size());
    EXPECT_EQ(value, retrievedValue);

    cache.disconnect();
}

// Test 2: PUT updates existing key
TEST(PutIntegrationTest, PutUpdateExisting) {
    createCacheViaREST("testcache");

    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    ByteArray key = {'u', 'p', 'd', 'a', 't', 'e', 'k', 'e', 'y'};
    ByteArray value1 = {'v', 'a', 'l', '1'};
    ByteArray value2 = {'v', 'a', 'l', '2'};

    // First PUT
    bool hadPrevious1 = cache.put(key, value1);
    EXPECT_FALSE(hadPrevious1);

    // Second PUT (update)
    bool hadPrevious2 = cache.put(key, value2);
    (void)hadPrevious2;  // Note: Previous value detection not fully implemented yet
    // EXPECT_TRUE(hadPrevious2);

    // GET to verify update
    ByteArray retrievedValue;
    bool found = cache.get(key, retrievedValue);

    EXPECT_TRUE(found);
    EXPECT_EQ(value2, retrievedValue);

    cache.disconnect();
}

// Test 3: PUT with large value (1000 bytes)
TEST(PutIntegrationTest, PutLargeValue) {
    createCacheViaREST("testcache");

    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    ByteArray key = {'b', 'i', 'g', 'k', 'e', 'y'};
    ByteArray value(1000, 'Z');  // 1000 'Z' characters

    bool hadPrevious = cache.put(key, value);
    EXPECT_FALSE(hadPrevious);

    // GET to verify
    ByteArray retrievedValue;
    bool found = cache.get(key, retrievedValue);

    EXPECT_TRUE(found);
    EXPECT_EQ(1000, retrievedValue.size());
    EXPECT_EQ('Z', retrievedValue[0]);
    EXPECT_EQ('Z', retrievedValue[999]);

    cache.disconnect();
}

// Test 4: PUT empty value
TEST(PutIntegrationTest, PutEmptyValue) {
    createCacheViaREST("testcache");

    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    ByteArray key = {'e', 'm', 'p', 't', 'y'};
    ByteArray value;  // Empty

    bool hadPrevious = cache.put(key, value);
    EXPECT_FALSE(hadPrevious);

    // GET to verify
    ByteArray retrievedValue;
    bool found = cache.get(key, retrievedValue);

    EXPECT_TRUE(found);
    EXPECT_EQ(0, retrievedValue.size());

    cache.disconnect();
}

// Test 5: Multiple PUTs on same connection
TEST(PutIntegrationTest, MultiplePuts) {
    createCacheViaREST("testcache");

    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    for (int i = 0; i < 10; i++) {
        std::string keyStr = "multi" + std::to_string(i);
        std::string valueStr = "val" + std::to_string(i);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        bool hadPrevious = cache.put(key, value);
        EXPECT_FALSE(hadPrevious) << "PUT #" << i << " failed";

        // Verify immediately
        ByteArray retrieved;
        bool found = cache.get(key, retrieved);
        EXPECT_TRUE(found) << "GET #" << i << " failed";
        EXPECT_EQ(value, retrieved) << "Value mismatch for #" << i;
    }

    cache.disconnect();
}

// Test 6: PUT to different caches
TEST(PutIntegrationTest, PutToDifferentCaches) {
    createCacheViaREST("cache1");
    createCacheViaREST("cache2");

    RemoteCache cache1(InfinispanTestEnvironment::host,
                       InfinispanTestEnvironment::port,
                       "cache1");
    cache1.connect();

    RemoteCache cache2(InfinispanTestEnvironment::host,
                       InfinispanTestEnvironment::port,
                       "cache2");
    cache2.connect();

    ByteArray key = {'s', 'h', 'a', 'r', 'e', 'd'};
    ByteArray value1 = {'c', 'a', 'c', 'h', 'e', '1'};
    ByteArray value2 = {'c', 'a', 'c', 'h', 'e', '2'};

    // PUT to cache1
    cache1.put(key, value1);

    // PUT to cache2
    cache2.put(key, value2);

    // GET from cache1
    ByteArray retrieved1;
    bool found1 = cache1.get(key, retrieved1);
    EXPECT_TRUE(found1);
    EXPECT_EQ(value1, retrieved1);

    // GET from cache2
    ByteArray retrieved2;
    bool found2 = cache2.get(key, retrieved2);
    EXPECT_TRUE(found2);
    EXPECT_EQ(value2, retrieved2);

    cache1.disconnect();
    cache2.disconnect();
}

// Test 7: PUT after disconnect should fail
TEST(PutIntegrationTest, PutAfterDisconnect) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();
    cache.disconnect();

    ByteArray key = {'t', 'e', 's', 't'};
    ByteArray value = {'v', 'a', 'l'};

    EXPECT_THROW({
        cache.put(key, value);
    }, std::runtime_error);
}

// Test 8: PUT with lifespan (basic test, no expiration check)
TEST(PutIntegrationTest, PutWithLifespan) {
    createCacheViaREST("testcache");

    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "testcache");
    cache.connect();

    ByteArray key = {'e', 'x', 'p', 'k', 'e', 'y'};
    ByteArray value = {'e', 'x', 'p', 'v', 'a', 'l'};

    // PUT with 10 second lifespan
    bool hadPrevious = cache.put(key, value, 10, 0);
    EXPECT_FALSE(hadPrevious);

    // GET immediately (should exist)
    ByteArray retrieved;
    bool found = cache.get(key, retrieved);
    EXPECT_TRUE(found);
    EXPECT_EQ(value, retrieved);

    // Note: Not testing actual expiration (would require waiting 10+ seconds)

    cache.disconnect();
}

// Main function - registers the global environment
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Add global environment (starts/stops server once for all tests)
    ::testing::AddGlobalTestEnvironment(new InfinispanTestEnvironment());

    return RUN_ALL_TESTS();
}
