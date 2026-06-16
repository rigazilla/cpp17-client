#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include <thread>
#include <chrono>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Topology Change Integration Tests
 *
 * Tests multi-server cluster topology changes:
 * - Add server (2 → 3 → 4 nodes)
 * - Remove server (4 → 3 → 2 nodes)
 * - Data accessibility after topology changes
 *
 * Based on Java tests:
 * - ReplTopologyChangeTest.java
 * - DistTopologyChangeTest.java
 */

// Test 1: Basic 3-node cluster formation
TEST_F(TopologyTest, ThreeNodeCluster) {
    // Verify 3 nodes are running (default)
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    // Verify cluster size from server perspective
    int clusterSize = getServerClusterSize();
    EXPECT_EQ(3, clusterSize) << "Server should report 3-node cluster";

    // Create client connected to node 1
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    // Verify connectivity with PING
    bool pingResult = cache->ping();
    EXPECT_TRUE(pingResult) << "PING should succeed";
}

// Test 2: Add server to cluster (3 → 4 nodes)
TEST_F(TopologyTest, AddServer) {
    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    auto cache = createDefaultClient();

    // Put some initial data
    for (int i = 0; i < 10; i++) {
        std::string key = "key-" + std::to_string(i);
        std::string value = "value-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes(value.begin(), value.end());

        cache->put(keyBytes, valueBytes);
    }

    // Add 4th node
    addNode(4);

    // Verify cluster size
    EXPECT_EQ(4, MultiServerTestEnvironment::numServers);
    EXPECT_EQ(4, getServerClusterSize());

    // Wait for topology update in client (perform operations)
    waitForTopologyUpdate(cache, 4);

    // Verify data is still accessible
    for (int i = 0; i < 10; i++) {
        std::string key = "key-" + std::to_string(i);
        std::string expectedValue = "value-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes;

        bool found = cache->get(keyBytes, valueBytes);
        ASSERT_TRUE(found) << "Key '" << key << "' should be found after adding node";

        std::string actualValue(valueBytes.begin(), valueBytes.end());
        EXPECT_EQ(expectedValue, actualValue) << "Value mismatch for key '" << key << "'";
    }

    // Put more data with 4-node topology
    for (int i = 10; i < 20; i++) {
        std::string key = "key-" + std::to_string(i);
        std::string value = "value-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes(value.begin(), value.end());

        cache->put(keyBytes, valueBytes);
    }

    // Verify all 20 keys
    for (int i = 0; i < 20; i++) {
        std::string key = "key-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes;

        bool found = cache->get(keyBytes, valueBytes);
        EXPECT_TRUE(found) << "Key '" << key << "' should be accessible with 4 nodes";
    }
}

// Test 3: Remove server from cluster (3 → 2 nodes)
TEST_F(TopologyTest, RemoveServer) {
    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    auto cache = createDefaultClient();

    // Put data
    for (int i = 0; i < 15; i++) {
        std::string key = "key-" + std::to_string(i);
        std::string value = "value-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes(value.begin(), value.end());

        cache->put(keyBytes, valueBytes);
    }

    // Remove node 3
    removeNode(3);

    // Verify cluster size
    EXPECT_EQ(2, MultiServerTestEnvironment::numServers);
    EXPECT_EQ(2, getServerClusterSize());

    // Wait for topology update
    waitForTopologyUpdate(cache, 2);

    // Verify data is still accessible (distributed with 2 owners, so data survives)
    for (int i = 0; i < 15; i++) {
        std::string key = "key-" + std::to_string(i);
        std::string expectedValue = "value-" + std::to_string(i);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes;

        bool found = cache->get(keyBytes, valueBytes);
        ASSERT_TRUE(found) << "Key '" << key << "' should be found after removing node";

        std::string actualValue(valueBytes.begin(), valueBytes.end());
        EXPECT_EQ(expectedValue, actualValue);
    }
}

// Test 4: Scale up then down (2 → 3 → 4 → 3 → 2)
TEST_F(TopologyTest, ScaleUpAndDown) {
    // Start with 2 nodes
    resetCluster(2);
    EXPECT_EQ(2, MultiServerTestEnvironment::numServers);
    EXPECT_EQ(2, getServerClusterSize());

    auto cache = createDefaultClient();

    // Put initial data
    std::vector<std::string> keys;
    for (int i = 0; i < 5; i++) {
        std::string key = "scale-key-" + std::to_string(i);
        keys.push_back(key);
        ByteArray keyBytes(key.begin(), key.end());
        ByteArray valueBytes = {(uint8_t)i};

        cache->put(keyBytes, valueBytes);
    }

    // Scale to 3 nodes
    addNode(3);
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);
    waitForTopologyUpdate(cache, 3);

    // Verify data
    for (int i = 0; i < 5; i++) {
        ByteArray keyBytes(keys[i].begin(), keys[i].end());
        ByteArray valueBytes;
        EXPECT_TRUE(cache->get(keyBytes, valueBytes)) << "Key lost after scale to 3";
    }

    // Scale to 4 nodes
    addNode(4);
    EXPECT_EQ(4, MultiServerTestEnvironment::numServers);
    waitForTopologyUpdate(cache, 4);

    // Verify data
    for (int i = 0; i < 5; i++) {
        ByteArray keyBytes(keys[i].begin(), keys[i].end());
        ByteArray valueBytes;
        EXPECT_TRUE(cache->get(keyBytes, valueBytes)) << "Key lost after scale to 4";
    }

    // Scale down to 3
    removeNode(4);
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);
    waitForTopologyUpdate(cache, 3);

    // Verify data
    for (int i = 0; i < 5; i++) {
        ByteArray keyBytes(keys[i].begin(), keys[i].end());
        ByteArray valueBytes;
        EXPECT_TRUE(cache->get(keyBytes, valueBytes)) << "Key lost after scale down to 3";
    }

    // Scale down to 2
    removeNode(3);
    EXPECT_EQ(2, MultiServerTestEnvironment::numServers);
    waitForTopologyUpdate(cache, 2);

    // Verify data (should still exist with 2 owners)
    for (int i = 0; i < 5; i++) {
        ByteArray keyBytes(keys[i].begin(), keys[i].end());
        ByteArray valueBytes;
        EXPECT_TRUE(cache->get(keyBytes, valueBytes)) << "Key lost after scale down to 2";
    }
}

// Test 5: Multiple clients see topology changes
TEST_F(TopologyTest, MultipleClientsSeeTopologyChange) {
    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    // Create clients connected to different servers
    auto client1 = createClient(0);  // Node 1
    auto client2 = createClient(1);  // Node 2
    auto client3 = createClient(2);  // Node 3

    // All clients should be able to PING
    EXPECT_TRUE(client1->ping());
    EXPECT_TRUE(client2->ping());
    EXPECT_TRUE(client3->ping());

    // Add 4th node
    addNode(4);

    // Wait for all clients to see topology update
    waitForTopologyUpdate(client1, 4, 10);
    waitForTopologyUpdate(client2, 4, 10);
    waitForTopologyUpdate(client3, 4, 10);

    // All clients should still work
    EXPECT_TRUE(client1->ping());
    EXPECT_TRUE(client2->ping());
    EXPECT_TRUE(client3->ping());

    // Put data via client1
    ByteArray key = {'m', 'u', 'l', 't', 'i'};
    ByteArray value = {'t', 'e', 's', 't'};
    client1->put(key, value);

    // Read via client2 (cross-client validation)
    ByteArray readValue;
    bool found = client2->get(key, readValue);
    EXPECT_TRUE(found);
    EXPECT_EQ(value, readValue);
}

// Test 6: Data survives node removal (distributed cache with 2 owners)
TEST_F(TopologyTest, DataSurvivesNodeRemoval) {
    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    auto cache = createDefaultClient();

    // Put 30 keys (distributed across 3 nodes)
    std::vector<ByteArray> keys;
    for (int i = 0; i < 30; i++) {
        std::string keyStr = "survive-" + std::to_string(i);
        ByteArray key(keyStr.begin(), keyStr.end());
        keys.push_back(key);

        ByteArray value = {(uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF)};
        cache->put(key, value);
    }

    // Remove node 3
    removeNode(3);
    EXPECT_EQ(2, MultiServerTestEnvironment::numServers);

    // All 30 keys should still be accessible (2 owners = data survives 1 node loss)
    int foundCount = 0;
    for (const auto& key : keys) {
        ByteArray value;
        if (cache->get(key, value)) {
            foundCount++;
        }
    }

    // With numOwners=2, all keys should survive losing 1 of 3 nodes
    EXPECT_EQ(30, foundCount) << "All keys should survive with 2 owners";
}

// Main function - registers the multi-server global environment
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Set initial cluster size (default: 3 nodes)
    // Can be overridden via command line: topology_integration_tests --initial-nodes=2
    MultiServerTestEnvironment::setInitialServerCount(3);

    // Add global environment (starts/stops cluster once for all tests)
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());

    return RUN_ALL_TESTS();
}
