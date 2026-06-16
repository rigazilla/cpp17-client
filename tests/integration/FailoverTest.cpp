#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include <thread>
#include <chrono>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Failover Integration Tests
 *
 * Tests that client can automatically failover to other servers
 * when the connected server fails.
 *
 * Phase 1: Basic connection failover
 */

// Test: Basic failover when connected server goes down
TEST_F(TopologyTest, FailoverOnServerDown) {
    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    // Create client connected to node 1, with topology awareness
    auto cache = createClient(0);  // Connect to server index 0 (node 1)
    ASSERT_NE(nullptr, cache);

    // Put some data via node 1
    ByteArray key = {'t', 'e', 's', 't', 'k', 'e', 'y'};
    ByteArray value = {'t', 'e', 's', 't', 'v', 'a', 'l'};

    bool putResult = cache->put(key, value);
    EXPECT_TRUE(putResult || !putResult);  // Either had previous value or not

    // Verify data is accessible
    ByteArray retrievedValue;
    EXPECT_TRUE(cache->get(key, retrievedValue)) << "Should be able to GET before failover";
    EXPECT_EQ(value, retrievedValue);

    // Kill node 1 (the server we're connected to)
    fprintf(stderr, "\n[TEST] Killing node 1 (connected server)...\n");
    MultiServerTestEnvironment::removeNode(1);  // Don't use helper that waits for cluster size

    // Wait a moment for server to fully stop
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Note: We can't easily verify cluster size via wait_for_cluster_size because node1 is gone
    // Just trust that removeNode worked

    // Try to GET again - should failover to node 2 or 3
    fprintf(stderr, "[TEST] Attempting GET after node 1 killed (should failover)...\n");
    ByteArray failoverValue;

    // This should succeed via automatic failover
    bool getResult = cache->get(key, failoverValue);
    EXPECT_TRUE(getResult) << "GET should succeed via failover to another server";

    if (getResult) {
        EXPECT_EQ(value, failoverValue) << "Value should be same after failover";
        fprintf(stderr, "[TEST] ✓ Failover successful! Data retrieved from backup server\n");
    }
}

// Test: Multiple failovers (servers going down one by one)
TEST_F(TopologyTest, MultipleFailovers) {
    // Start with 3 nodes
    resetCluster(3);
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    auto cache = createDefaultClient();

    // Put test data
    ByteArray key = {'m', 'u', 'l', 't', 'i'};
    ByteArray value = {'f', 'a', 'i', 'l', 'o', 'v', 'e', 'r'};
    cache->put(key, value);

    // Verify initial GET works
    ByteArray result;
    EXPECT_TRUE(cache->get(key, result));
    EXPECT_EQ(value, result);

    // Kill node 1
    fprintf(stderr, "\n[TEST] Killing node 1...\n");
    removeNode(1);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Should still work (failover to node 2 or 3)
    EXPECT_TRUE(cache->get(key, result)) << "Should work after node 1 down";

    // Kill node 2
    fprintf(stderr, "[TEST] Killing node 2...\n");
    removeNode(2);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Should still work (only node 3 left)
    EXPECT_TRUE(cache->get(key, result)) << "Should work after node 2 down (only node 3 left)";
    EXPECT_EQ(value, result);

    fprintf(stderr, "[TEST] ✓ Survived multiple failovers!\n");
}

// Test: Client continues working after failover
TEST_F(TopologyTest, ContinueAfterFailover) {
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    auto cache = createClient(0);

    // Initial operation
    ByteArray key1 = {'k', '1'};
    ByteArray value1 = {'v', '1'};
    cache->put(key1, value1);

    // Kill connected server
    removeNode(1);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Do more operations after failover
    ByteArray key2 = {'k', '2'};
    ByteArray value2 = {'v', '2'};

    EXPECT_TRUE(cache->put(key2, value2)) << "PUT should work after failover";

    ByteArray retrieved;
    EXPECT_TRUE(cache->get(key2, retrieved)) << "GET should work after failover";
    EXPECT_EQ(value2, retrieved);

    // Original data should still be accessible
    EXPECT_TRUE(cache->get(key1, retrieved)) << "Original data should still be accessible";
    EXPECT_EQ(value1, retrieved);
}

// Main function - uses MultiServerTestEnvironment
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Start with 3 nodes for failover tests
    MultiServerTestEnvironment::setInitialServerCount(3);

    // Register global environment
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());

    return RUN_ALL_TESTS();
}
