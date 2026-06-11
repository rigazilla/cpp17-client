#include <gtest/gtest.h>
#include "hotrod/RemoteCache.h"
#include <iostream>

/**
 * PING Integration Tests
 *
 * These tests require a running Infinispan server.
 *
 * Manual testing:
 *   docker run -d -p 11222:11222 -e USER=admin -e PASS=password infinispan/server:16.0
 *   ./build/integration_tests
 *
 * TODO: Use Testcontainers library for automatic server lifecycle
 */

using namespace hotrod;

// Helper: Check if server is available
bool isServerAvailable(const std::string& host, uint16_t port) {
    try {
        RemoteCache cache(host, port);
        cache.connect();
        cache.disconnect();
        return true;
    } catch (...) {
        return false;
    }
}

class PingIntegrationTest : public ::testing::Test {
protected:
    static constexpr const char* TEST_HOST = "localhost";
    static constexpr uint16_t TEST_PORT = 11222;

    void SetUp() override {
        // Check if server is available
        if (!isServerAvailable(TEST_HOST, TEST_PORT)) {
            GTEST_SKIP() << "Infinispan server not available at "
                         << TEST_HOST << ":" << TEST_PORT
                         << ". Start with: docker run -d -p 11222:11222 "
                         << "-e USER=admin -e PASS=password infinispan/server:16.0";
        }
    }
};

// Test 1: Basic PING to default cache
TEST_F(PingIntegrationTest, BasicPing) {
    RemoteCache cache(TEST_HOST, TEST_PORT);
    cache.connect();

    bool result = cache.ping();

    EXPECT_TRUE(result);

    cache.disconnect();
}

// Test 2: PING with named cache
TEST_F(PingIntegrationTest, PingWithNamedCache) {
    RemoteCache cache(TEST_HOST, TEST_PORT, "myCache");
    cache.connect();

    bool result = cache.ping();

    EXPECT_TRUE(result);

    cache.disconnect();
}

// Test 3: Multiple PINGs on same connection
TEST_F(PingIntegrationTest, MultiplePings) {
    RemoteCache cache(TEST_HOST, TEST_PORT);
    cache.connect();

    for (int i = 0; i < 10; i++) {
        bool result = cache.ping();
        EXPECT_TRUE(result) << "PING #" << i << " failed";
    }

    cache.disconnect();
}

// Test 4: PING after disconnect should fail
TEST_F(PingIntegrationTest, PingAfterDisconnect) {
    RemoteCache cache(TEST_HOST, TEST_PORT);
    cache.connect();
    cache.disconnect();

    EXPECT_THROW({
        cache.ping();
    }, std::runtime_error);
}

// Test 5: Connect, PING, disconnect, reconnect, PING
TEST_F(PingIntegrationTest, ReconnectAndPing) {
    RemoteCache cache(TEST_HOST, TEST_PORT);

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
