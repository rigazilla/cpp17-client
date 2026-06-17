#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include "RestAPIHelper.h"
#include "hotrod/RemoteCache.h"
#include "hotrod/MurmurHash3.h"
#include <thread>
#include <chrono>
#include <map>
#include <set>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Integration tests for hash-aware routing (client intelligence 0x03).
 *
 * These tests verify that keys actually route to the correct primary owner
 * in a real Infinispan cluster, using REST API to verify server-side state.
 *
 * Java Reference:
 * - org.infinispan.client.hotrod.ConsistentHashV2IntegrationTest
 * - org.infinispan.client.hotrod.ReplTopologyChangeTest
 * - org.infinispan.client.hotrod.DistTopologyChangeTest
 */

class HashAwareRoutingIntegrationTest : public TopologyTest {
protected:
    void SetUp() override {
        TopologyTest::SetUp();

        // Ensure we start with 3 nodes
        resetCluster(3);

        // Clear any existing test data
        clearTestCache();
    }

    /**
     * Helper: Calculate expected segment for a key (same as client logic).
     */
    int calculateSegment(const ByteArray& key, int numSegments = 256) {
        int32_t hash = MurmurHash3::hash32(key, 9001);
        int normalizedHash = hash & 0x7FFFFFFF;
        return normalizedHash % numSegments;
    }

    /**
     * Helper: Get the primary owner for a key via REST API.
     * Returns the primary owner's IP address (without port).
     */
    std::string getPrimaryOwnerForKey(const std::string& cacheName, const std::string& key) {
        if (MultiServerTestEnvironment::servers.empty()) {
            return "";
        }

        const auto& server = MultiServerTestEnvironment::getServer(0);

        // REST API v3: GET /rest/v3/caches/{cacheName}/_distribution/{key}
        std::string url = "http://" + server.host + ":" + std::to_string(server.port) +
                         "/rest/v3/caches/" + cacheName + "/_distribution/" + key;

        std::string cmd = "curl -s -u admin:password \"" + url + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";

        char buffer[4096];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        // Parse JSON to find primary owner's node_addresses
        // Look for "primary": true and then extract the IP from node_addresses
        size_t primaryPos = result.find("\"primary\": true");
        if (primaryPos == std::string::npos) {
            primaryPos = result.find("\"primary\":true");
        }
        if (primaryPos == std::string::npos) return "";

        // Find node_addresses array before this primary marker
        // Search backwards from primaryPos to find the node_addresses for this owner
        size_t searchStart = (primaryPos > 500) ? primaryPos - 500 : 0;
        std::string searchArea = result.substr(searchStart, primaryPos - searchStart + 100);

        size_t addrPos = searchArea.rfind("\"node_addresses\"");
        if (addrPos == std::string::npos) return "";

        // Find the IP address in the array (format: "172.18.0.X:7800")
        size_t ipStart = searchArea.find("\"", addrPos + 16);
        if (ipStart == std::string::npos) return "";
        ipStart++;

        size_t ipEnd = searchArea.find(":", ipStart);
        if (ipEnd == std::string::npos) return "";

        return searchArea.substr(ipStart, ipEnd - ipStart);
    }

    /**
     * Helper: Get key count on each server via REST API.
     * Uses topology addresses received by the client.
     */
    std::map<int, int> getKeyCountsPerServer(RemoteCache* cache) {
        std::map<int, int> counts;

        const auto& topology = cache->getTopology();
        const auto& servers = topology.getServers();

        for (size_t i = 0; i < servers.size(); i++) {
            const auto& server = servers[i];
            // Use TEST_CACHE (same cache as Hot Rod client)
            int count = RestAPIHelper::getKeyCount(server.host, server.port, TEST_CACHE);

            fprintf(stderr, "[REST] Server %d (%s:%d) has %d keys\n",
                    static_cast<int>(i), server.host.c_str(), server.port, count);

            counts[static_cast<int>(i)] = count;
        }

        return counts;
    }

    /**
     * Helper: Clear test cache on all servers.
     */
    void clearAllServers() {
        for (int i = 0; i < MultiServerTestEnvironment::numServers; i++) {
            const auto& server = MultiServerTestEnvironment::getServer(i);
            // Use TEST_CACHE
            RestAPIHelper::clearCache(server.host, server.port, TEST_CACHE);
        }

        // Give servers time to sync
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

/**
 * Test 1: Keys route to correct primary owner (CRITICAL).
 *
 * Verifies that with HASH_DISTRIBUTION_AWARE intelligence, keys actually
 * go to the server that owns their segment (not randomly distributed).
 *
 * Java reference: ConsistentHashV2IntegrationTest.testHashDistribution()
 */
TEST_F(HashAwareRoutingIntegrationTest, KeysRouteToCorrectPrimaryOwner) {
    fprintf(stderr, "\n=== Test 1: KeysRouteToCorrectPrimaryOwner ===\n");

    // Create client with HASH_DISTRIBUTION_AWARE intelligence
    // Note: createDefaultClient() already calls connect() internally
    // Use TEST_CACHE which was created in SetUp() via createTestCache()
    auto cache = createDefaultClient();  // Uses TEST_CACHE by default
    ASSERT_NE(nullptr, cache);

    // Client intelligence already set by createClient()
    // Topology will be received on first operation

    // Clear any existing data
    clearAllServers();

    // PUT 100 keys with hash-aware routing (topology will be received during first PUT)
    const int NUM_KEYS = 100;
    std::map<int, std::vector<std::string>> keysBySegment;

    fprintf(stderr, "[TEST] Putting %d keys with hash-aware routing...\n", NUM_KEYS);

    for (int i = 0; i < NUM_KEYS; i++) {
        std::string keyStr = "hashkey-" + std::to_string(i);
        std::string valueStr = "hashvalue-" + std::to_string(i);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        // Calculate segment
        int segment = calculateSegment(key);
        keysBySegment[segment].push_back(keyStr);

        // PUT via Hot Rod (should route to primary owner)
        cache->put(key, value);
    }

    fprintf(stderr, "[TEST] Keys distributed across %zu segments\n", keysBySegment.size());

    // Verify we received hash topology after the PUT
    EXPECT_TRUE(cache->getConsistentHash().hasHashTopology())
        << "Client should have hash topology with intelligence 0x03";

    EXPECT_EQ(256, cache->getConsistentHash().getNumSegments())
        << "Should have 256 segments";

    // Give cluster time to sync
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify that each key was routed to the correct primary owner
    fprintf(stderr, "[TEST] Verifying hash-aware routing...\n");

    int matchCount = 0;
    for (int i = 0; i < NUM_KEYS; i++) {
        std::string keyStr = "hashkey-" + std::to_string(i);

        // Get the server the client chose
        ByteArray key(keyStr.begin(), keyStr.end());
        const ServerInfo* primaryOwner = cache->getConsistentHash().getPrimaryOwner(key, cache->getTopology());
        ASSERT_NE(nullptr, primaryOwner) << "Should have primary owner for key " << keyStr;

        // Get the actual primary owner from server via REST
        std::string serverPrimaryIP = getPrimaryOwnerForKey(TEST_CACHE, keyStr);

        // Verify they match
        if (primaryOwner->host == serverPrimaryIP) {
            matchCount++;
        } else {
            fprintf(stderr, "[MISMATCH] Key '%s': Client chose %s, Server says %s\n",
                    keyStr.c_str(), primaryOwner->host.c_str(), serverPrimaryIP.c_str());
        }
    }

    fprintf(stderr, "[RESULT] %d/%d keys routed correctly\n", matchCount, NUM_KEYS);
    EXPECT_EQ(NUM_KEYS, matchCount) << "All keys should route to correct primary owner";

    fprintf(stderr, "[PASS] Keys correctly routed to primary owners\n");
}

/**
 * Test 3: Failover to backup owner when primary is down (CRITICAL).
 *
 * Verifies that when the primary owner fails, the client can still access
 * the key from the backup owner (numOwners=2).
 *
 * Java reference: ReplTopologyChangeTest.testDropServer()
 */
TEST_F(HashAwareRoutingIntegrationTest, FailoverToBackupOwner) {
    fprintf(stderr, "\n=== Test 3: FailoverToBackupOwner ===\n");

    // Start with 3 nodes
    EXPECT_EQ(3, MultiServerTestEnvironment::numServers);

    // Create client with HASH_DISTRIBUTION_AWARE
    // Note: createDefaultClient() already calls connect() internally
    // Use TEST_CACHE which was created in SetUp()
    auto cache = createDefaultClient();  // Uses TEST_CACHE by default
    ASSERT_NE(nullptr, cache);

    // Client intelligence already set, wait for topology
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Clear data
    clearAllServers();

    // PUT a test key
    std::string keyStr = "failover-test-key";
    std::string valueStr = "failover-test-value";
    ByteArray key(keyStr.begin(), keyStr.end());
    ByteArray value(valueStr.begin(), valueStr.end());

    fprintf(stderr, "[TEST] Putting key: %s\n", keyStr.c_str());
    cache->put(key, value);

    // Give time for replication (numOwners=2)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify key exists
    ByteArray retrievedValue;
    bool found = cache->get(key, retrievedValue);
    ASSERT_TRUE(found) << "Key should exist after PUT";
    EXPECT_EQ(value, retrievedValue) << "Retrieved value should match";

    // Calculate which segment this key belongs to
    int segment = calculateSegment(key);
    fprintf(stderr, "[TEST] Key '%s' maps to segment %d\n", keyStr.c_str(), segment);

    // Get primary and backup owners from ConsistentHash
    const ServerInfo* primaryOwner = cache->getConsistentHash().getPrimaryOwner(key, cache->getTopology());
    ASSERT_NE(nullptr, primaryOwner) << "Should have primary owner";

    fprintf(stderr, "[TEST] Primary owner: %s:%u (hashId=%d)\n",
            primaryOwner->host.c_str(), primaryOwner->port, primaryOwner->hashId);

    // Get all owners (primary + backup)
    auto owners = cache->getConsistentHash().getOwners(key, cache->getTopology());
    ASSERT_GE(owners.size(), 2) << "Should have at least 2 owners (numOwners=2)";

    const ServerInfo* backupOwner = owners[1];
    fprintf(stderr, "[TEST] Backup owner: %s:%u (hashId=%d)\n",
            backupOwner->host.c_str(), backupOwner->port, backupOwner->hashId);

    // Find which node number is the primary owner by looking at topology index
    // The topology servers are ordered node1, node2, node3... so index maps to node number
    const auto& topologyServers = cache->getTopology().getServers();
    int primaryNodeNum = -1;

    for (size_t i = 0; i < topologyServers.size(); i++) {
        if (topologyServers[i].hashId == primaryOwner->hashId) {
            primaryNodeNum = i + 1;  // Node numbers are 1-based
            break;
        }
    }

    ASSERT_NE(-1, primaryNodeNum) << "Should find primary owner in topology";

    fprintf(stderr, "[TEST] Killing primary owner node %d...\n", primaryNodeNum);

    // Kill the primary owner
    removeNode(primaryNodeNum);

    // Wait for topology update
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Try to GET the key (should failover to backup)
    fprintf(stderr, "[TEST] Attempting GET after primary owner failure...\n");

    ByteArray failoverValue;
    bool foundAfterFailover = false;

    // Retry a few times in case of transient issues
    for (int attempt = 0; attempt < 3; attempt++) {
        try {
            foundAfterFailover = cache->get(key, failoverValue);
            if (foundAfterFailover) {
                break;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[WARN] GET attempt %d failed: %s\n", attempt + 1, e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    EXPECT_TRUE(foundAfterFailover)
        << "Key should still be accessible after primary owner failure (via backup)";

    if (foundAfterFailover) {
        EXPECT_EQ(value, failoverValue)
            << "Retrieved value should match original after failover";

        fprintf(stderr, "[PASS] Failover to backup owner successful\n");
    }

    // Cleanup: restore node
    addNode(primaryNodeNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

/**
 * Test 4: Topology rebalance updates routing (CRITICAL).
 *
 * Verifies that when the cluster topology changes (add/remove node),
 * the client updates its routing and keys remain accessible.
 *
 * Java reference: DistTopologyChangeTest.testNewTopologyIdPerTopologyUpdate()
 */
TEST_F(HashAwareRoutingIntegrationTest, TopologyRebalanceUpdatesRouting) {
    fprintf(stderr, "\n=== Test 4: TopologyRebalanceUpdatesRouting ===\n");

    // Start with 2 nodes
    resetCluster(2);
    clearAllServers();

    // Create client
    // Note: createDefaultClient() already calls connect() internally
    // Use TEST_CACHE which was created in SetUp()
    auto cache = createDefaultClient();  // Uses TEST_CACHE by default
    ASSERT_NE(nullptr, cache);

    // Client intelligence already set, wait for topology
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Record initial topology ID
    int initialTopoId = cache->getTopologyId();
    fprintf(stderr, "[TEST] Initial topology ID: %d (2 nodes)\n", initialTopoId);

    // PUT 50 keys with 2-node cluster
    const int NUM_KEYS = 50;
    std::vector<std::string> testKeys;

    fprintf(stderr, "[TEST] Putting %d keys with 2-node cluster...\n", NUM_KEYS);

    for (int i = 0; i < NUM_KEYS; i++) {
        std::string keyStr = "rebalance-key-" + std::to_string(i);
        std::string valueStr = "rebalance-value-" + std::to_string(i);

        testKeys.push_back(keyStr);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        cache->put(key, value);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify all keys accessible with 2 nodes
    int keysFoundBefore = 0;
    for (const auto& keyStr : testKeys) {
        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value;
        if (cache->get(key, value)) {
            keysFoundBefore++;
        }
    }

    EXPECT_EQ(NUM_KEYS, keysFoundBefore)
        << "All keys should be accessible with 2 nodes";

    fprintf(stderr, "[TEST] All %d keys accessible with 2 nodes\n", keysFoundBefore);

    // Add 3rd node (triggers rebalancing)
    fprintf(stderr, "[TEST] Adding 3rd node (triggers rebalance)...\n");
    addNode(3);

    // Wait for rebalance to complete
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Trigger topology update by doing an operation
    ByteArray dummyKey = {'d', 'u', 'm', 'm', 'y'};
    ByteArray dummyValue;
    cache->get(dummyKey, dummyValue);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Verify topology ID changed
    int newTopoId = cache->getTopologyId();
    fprintf(stderr, "[TEST] New topology ID: %d (3 nodes)\n", newTopoId);

    EXPECT_NE(initialTopoId, newTopoId)
        << "Topology ID should change after adding node";

    // Verify all keys still accessible after rebalance
    int keysFoundAfter = 0;
    std::vector<std::string> missingKeys;

    for (const auto& keyStr : testKeys) {
        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value;

        bool found = false;
        // Retry a few times in case rebalance still ongoing
        for (int attempt = 0; attempt < 3; attempt++) {
            try {
                found = cache->get(key, value);
                if (found) break;
            } catch (...) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (found) {
            keysFoundAfter++;
        } else {
            missingKeys.push_back(keyStr);
        }
    }

    if (!missingKeys.empty()) {
        fprintf(stderr, "[WARN] Missing keys after rebalance: %zu\n", missingKeys.size());
        for (const auto& key : missingKeys) {
            fprintf(stderr, "[WARN]   - %s\n", key.c_str());
        }
    }

    EXPECT_EQ(NUM_KEYS, keysFoundAfter)
        << "All keys should still be accessible after rebalance";

    fprintf(stderr, "[PASS] Topology rebalance successful: %d/%d keys accessible\n",
            keysFoundAfter, NUM_KEYS);

    // Verify keys redistributed across 3 servers
    auto counts = getKeyCountsPerServer(cache);
    int serversWithKeys = 0;
    for (const auto& entry : counts) {
        if (entry.second > 0) {
            serversWithKeys++;
        }
    }

    EXPECT_GE(serversWithKeys, 2)
        << "Keys should be distributed across multiple servers after rebalance";
}

// Main function - registers the multi-server global environment
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Set initial cluster size (default: 3 nodes for hash-aware tests)
    MultiServerTestEnvironment::setInitialServerCount(3);

    // Add global environment (starts/stops cluster once for all tests)
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());

    return RUN_ALL_TESTS();
}
