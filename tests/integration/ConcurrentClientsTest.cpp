#include <gtest/gtest.h>
#include "hotrod/RemoteCache.h"
#include "InfinispanTestEnvironment.h"
#include <thread>
#include <vector>
#include <string>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Concurrent Clients Integration Test
 *
 * Tests multiple clients (with BASIC intelligence) performing operations concurrently
 * against a single Infinispan server.
 *
 * Purpose: Verify basic concurrent access patterns without topology awareness.
 */

namespace {

// Helper: Create cache via CLI
void createCacheViaCLI(const std::string& cacheName) {
    std::string cmd = "docker exec " + InfinispanTestEnvironment::containerID +
                     " bash -c \"echo 'create cache --template=org.infinispan.DIST_SYNC " + cacheName +
                     "' | /opt/infinispan/bin/cli.sh -c http://admin:password@localhost:11222\" >/dev/null 2>&1";
    system(cmd.c_str());
}

} // anonymous namespace

// Test: 4 concurrent clients, each performing 10 PUTs and 10 GETs
TEST(ConcurrentClientsTest, FourClientsBasicIntelligence) {
    // Ensure cache exists
    const std::string cacheName = "concurrent-test";
    createCacheViaCLI(cacheName);

    const int NUM_CLIENTS = 4;
    const int OPS_PER_CLIENT = 10;

    std::vector<std::thread> threads;
    std::vector<bool> results(NUM_CLIENTS, true);

    // Lambda: Each client performs 10 PUTs then 10 GETs
    auto clientWork = [&](int clientId) {
        try {
            // Create client with BASIC intelligence (default)
            RemoteCache cache(InfinispanTestEnvironment::host,
                            InfinispanTestEnvironment::port,
                            cacheName);

            // Explicitly set BASIC intelligence (no topology awareness)
            cache.setClientIntelligence(ClientIntelligence::BASIC);

            cache.connect();

            // Perform 10 PUTs
            for (int i = 0; i < OPS_PER_CLIENT; i++) {
                std::string keyStr = "client" + std::to_string(clientId) +
                                    "-key" + std::to_string(i);
                std::string valueStr = "client" + std::to_string(clientId) +
                                      "-value" + std::to_string(i);

                ByteArray key(keyStr.begin(), keyStr.end());
                ByteArray value(valueStr.begin(), valueStr.end());

                cache.put(key, value);
            }

            // Perform 10 GETs (verify what we just wrote)
            for (int i = 0; i < OPS_PER_CLIENT; i++) {
                std::string keyStr = "client" + std::to_string(clientId) +
                                    "-key" + std::to_string(i);
                std::string expectedValueStr = "client" + std::to_string(clientId) +
                                              "-value" + std::to_string(i);

                ByteArray key(keyStr.begin(), keyStr.end());
                ByteArray retrievedValue;

                bool found = cache.get(key, retrievedValue);
                if (!found) {
                    results[clientId] = false;
                    return;
                }

                std::string actualValueStr(retrievedValue.begin(), retrievedValue.end());
                if (actualValueStr != expectedValueStr) {
                    results[clientId] = false;
                    return;
                }
            }

            cache.disconnect();

        } catch (const std::exception& e) {
            results[clientId] = false;
        }
    };

    // Launch 4 concurrent clients
    for (int i = 0; i < NUM_CLIENTS; i++) {
        threads.emplace_back(clientWork, i);
    }

    // Wait for all clients to finish
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all clients succeeded
    for (int i = 0; i < NUM_CLIENTS; i++) {
        EXPECT_TRUE(results[i]) << "Client " << i << " failed";
    }
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Register single-server test environment
    ::testing::AddGlobalTestEnvironment(new InfinispanTestEnvironment());

    return RUN_ALL_TESTS();
}
