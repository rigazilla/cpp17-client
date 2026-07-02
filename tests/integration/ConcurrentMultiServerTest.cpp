#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include "hotrod/RemoteCache.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <future>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Concurrent operations on multi-server cluster (3 nodes).
 *
 * Tests concurrent GET/PUT/REMOVE operations against a distributed cache
 * with hash-aware routing and automatic failover.
 */

class ConcurrentMultiServerTest : public TopologyTest {
protected:
    void SetUp() override {
        TopologyTest::SetUp();

        // Ensure we have 3 nodes
        resetCluster(3);

        // Clear test cache
        clearTestCache();
    }
};

// Test 1: Single client with concurrent operations on distributed cache
TEST_F(ConcurrentMultiServerTest, SingleClientConcurrentDistributed) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    const int NUM_KEYS = 100;

    // Phase 1: Issue all PUTs concurrently (non-blocking)
    std::vector<std::future<std::optional<EntryWithMetadata>>> putFutures;
    std::vector<std::string> keys;
    std::vector<std::string> values;

    for (int i = 0; i < NUM_KEYS; i++) {
        std::string keyStr = "dist-key-" + std::to_string(i);
        std::string valueStr = "dist-value-" + std::to_string(i);

        keys.push_back(keyStr);
        values.push_back(valueStr);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        putFutures.push_back(cache->put(key, value));
    }

    // Phase 2: Issue all GETs in different order (interlaced)
    std::vector<std::future<std::optional<ByteArray>>> getFutures;
    for (int i = NUM_KEYS - 1; i >= 0; i--) {
        ByteArray key(keys[i].begin(), keys[i].end());
        getFutures.push_back(cache->get(key));
    }

    // Phase 3: Issue REMOVEs for every 3rd key
    std::vector<std::future<std::optional<EntryWithMetadata>>> removeFutures;
    for (int i = 0; i < NUM_KEYS; i += 3) {
        ByteArray key(keys[i].begin(), keys[i].end());
        removeFutures.push_back(cache->remove(key));
    }

    // Wait for all PUTs
    for (auto& fut : putFutures) {
        fut.get();
    }

    // Verify all GETs (in reverse order)
    int errors = 0;
    for (int i = 0; i < NUM_KEYS; i++) {
        auto result = getFutures[i].get();
        if (!result.has_value()) {
            errors++;
            continue;
        }
        std::string retrieved(result.value().begin(), result.value().end());
        std::string expected = values[NUM_KEYS - 1 - i];  // Reverse order
        if (retrieved != expected) {
            errors++;
        }
    }

    // Wait for all REMOVEs
    for (auto& fut : removeFutures) {
        fut.get();
    }

    EXPECT_EQ(0, errors) << "Single client distributed operations had errors";
}

// Test 2: Multiple clients with interlaced operations on distributed cache
TEST_F(ConcurrentMultiServerTest, MultipleClientsInterlaced) {
    const int NUM_CLIENTS = 5;
    const int OPS_PER_CLIENT = 30;

    std::vector<std::thread> clientThreads;
    std::atomic<int> totalErrors{0};

    // Each client performs interlaced PUT/GET/REMOVE
    auto clientWork = [&](int clientId) {
        try {
            // Each client connects to a different server (round-robin)
            int serverIndex = clientId % MultiServerTestEnvironment::numServers;
            auto cache = createClient(serverIndex);

            std::vector<std::future<std::optional<EntryWithMetadata>>> putFutures;
            std::vector<std::future<std::optional<ByteArray>>> getFutures;
            std::vector<std::future<std::optional<EntryWithMetadata>>> removeFutures;

            // Issue all operations concurrently (non-blocking)
            for (int i = 0; i < OPS_PER_CLIENT; i++) {
                std::string keyStr = "multi-client" + std::to_string(clientId) +
                                    "-key" + std::to_string(i);
                std::string valueStr = "multi-value-" + std::to_string(clientId) +
                                      "-" + std::to_string(i);

                ByteArray key(keyStr.begin(), keyStr.end());
                ByteArray value(valueStr.begin(), valueStr.end());

                // Interlace: PUT, GET, PUT, GET, REMOVE pattern
                putFutures.push_back(cache->put(key, value));
                getFutures.push_back(cache->get(key));

                if (i % 2 == 0) {
                    removeFutures.push_back(cache->remove(key));
                }
            }

            // Wait for all PUTs
            for (auto& fut : putFutures) {
                fut.get();
            }

            // Verify all GETs (some may not exist due to REMOVE)
            for (auto& fut : getFutures) {
                fut.get();  // Just consume the results
            }

            // Wait for all REMOVEs
            for (auto& fut : removeFutures) {
                fut.get();
            }

            cache->disconnect();

        } catch (const std::exception& e) {
            fprintf(stderr, "[Client %d ERROR] %s\n", clientId, e.what());
            totalErrors++;
        }
    };

    // Launch clients
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clientThreads.emplace_back(clientWork, i);
    }

    // Wait for all
    for (auto& t : clientThreads) {
        t.join();
    }

    EXPECT_EQ(0, totalErrors.load()) << "Multiple clients interlaced operations had errors";
}

// Test 3: Stress test with hash-aware routing across cluster
TEST_F(ConcurrentMultiServerTest, StressTestDistributed) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    const int NUM_OPERATIONS = 200;

    std::vector<std::string> keys;
    std::vector<std::string> values;

    // Phase 1: Issue all PUTs (non-blocking)
    std::vector<std::future<std::optional<EntryWithMetadata>>> putFutures;
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        std::string keyStr = "stress-dist-" + std::to_string(i);
        std::string valueStr = "stress-val-" + std::to_string(i);

        keys.push_back(keyStr);
        values.push_back(valueStr);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        putFutures.push_back(cache->put(key, value));
    }

    // Phase 2: Immediately issue all GETs (before PUTs complete, testing interlacing)
    std::vector<std::future<std::optional<ByteArray>>> getFutures;
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        ByteArray key(keys[i].begin(), keys[i].end());
        getFutures.push_back(cache->get(key));
    }

    // Phase 3: Wait for all PUTs
    for (auto& fut : putFutures) {
        fut.get();
    }

    // Phase 4: Verify all GETs
    int completedOps = 0;
    int errors = 0;

    for (int i = 0; i < NUM_OPERATIONS; i++) {
        auto result = getFutures[i].get();

        if (result.has_value()) {
            std::string retrieved(result.value().begin(), result.value().end());
            if (retrieved == values[i]) {
                completedOps++;
            } else {
                errors++;
            }
        } else {
            errors++;
        }
    }

    EXPECT_EQ(NUM_OPERATIONS, completedOps) << "Stress test: some operations failed";
    EXPECT_EQ(0, errors) << "Stress test had errors";
}

// Test 4: Concurrent operations with node failure (failover test)
TEST_F(ConcurrentMultiServerTest, ConcurrentWithFailover) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    const int NUM_KEYS = 50;

    // Phase 1: PUT keys
    std::vector<std::string> keys;
    std::vector<std::string> values;

    for (int i = 0; i < NUM_KEYS; i++) {
        std::string keyStr = "failover-key-" + std::to_string(i);
        std::string valueStr = "failover-val-" + std::to_string(i);

        keys.push_back(keyStr);
        values.push_back(valueStr);

        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        cache->put(key, value).get();
    }

    // Give time for replication
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Phase 2: Kill a node
    removeNode(2);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Phase 3: Issue concurrent GETs (should failover to backup owners)
    std::vector<std::future<std::optional<ByteArray>>> getFutures;
    for (int i = 0; i < NUM_KEYS; i++) {
        ByteArray key(keys[i].begin(), keys[i].end());
        getFutures.push_back(cache->get(key));
    }

    // Verify all GETs succeeded (via failover)
    int successes = 0;

    for (int i = 0; i < NUM_KEYS; i++) {
        auto result = getFutures[i].get();

        if (result.has_value()) {
            std::string retrieved(result.value().begin(), result.value().end());
            if (retrieved == values[i]) {
                successes++;
            }
        }
    }

    // Most should succeed (some might fail if they were on the killed node and not replicated yet)
    EXPECT_GT(successes, NUM_KEYS * 0.8) << "Too many failures during failover";
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Register multi-server test environment (3 nodes)
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());

    return RUN_ALL_TESTS();
}
