#include <gtest/gtest.h>
#include "hotrod/RemoteCache.h"
#include "InfinispanTestEnvironment.h"

using namespace hotrod;
using namespace hotrod::test;

/**
 * PING Integration Tests - No Authentication
 *
 * Tests run against Infinispan server managed by InfinispanTestEnvironment.
 * Server lifecycle: SetUp once before all tests, TearDown once after all tests.
 */

// Test 1: Basic PING to default cache
TEST(PingIntegrationTest, BasicPing) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port);
    cache.connect();

    bool result = cache.ping();

    EXPECT_TRUE(result);

    cache.disconnect();
}

// Test 2: PING with empty cache name (same as default)
TEST(PingIntegrationTest, PingWithEmptyCacheName) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port,
                      "");  // Empty cache name = default cache
    cache.connect();

    bool result = cache.ping();

    EXPECT_TRUE(result);

    cache.disconnect();
}

// Test 3: Multiple PINGs on same connection
TEST(PingIntegrationTest, MultiplePings) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port);
    cache.connect();

    for (int i = 0; i < 10; i++) {
        bool result = cache.ping();
        EXPECT_TRUE(result) << "PING #" << i << " failed";
    }

    cache.disconnect();
}

// Test 4: PING after disconnect should fail
TEST(PingIntegrationTest, PingAfterDisconnect) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port);
    cache.connect();
    cache.disconnect();

    EXPECT_THROW({
        cache.ping();
    }, std::runtime_error);
}

// Test 5: Reconnect and PING
TEST(PingIntegrationTest, ReconnectAndPing) {
    RemoteCache cache(InfinispanTestEnvironment::host,
                      InfinispanTestEnvironment::port);

    // First connection
    cache.connect();
    bool result1 = cache.ping();
    EXPECT_TRUE(result1);
    cache.disconnect();

    // Reconnect
    cache.connect();
    bool result2 = cache.ping();
    EXPECT_TRUE(result2);
    cache.disconnect();
}

// Main function - registers the global environment
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Add global environment (starts/stops server once for all tests)
    ::testing::AddGlobalTestEnvironment(new InfinispanTestEnvironment());

    return RUN_ALL_TESTS();
}
