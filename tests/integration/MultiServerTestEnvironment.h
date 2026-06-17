#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <sstream>

namespace hotrod {
namespace test {

/**
 * Global test environment for multi-server Infinispan topology tests.
 *
 * Manages cluster lifecycle:
 * - SetUp(): Start N-node cluster (default: 3)
 * - TearDown(): Stop cluster
 *
 * Static methods for dynamic topology changes:
 * - addNode(): Add node to running cluster
 * - removeNode(): Remove node from cluster
 * - waitForClusterSize(): Wait for cluster to reach expected size
 *
 * Server info stored in static members, accessible from all tests.
 */
class MultiServerTestEnvironment : public ::testing::Environment {
public:
    struct ServerInfo {
        std::string host;
        int port;
        std::string containerID;

        ServerInfo() : port(0) {}

        ServerInfo(const std::string& h, int p, const std::string& c)
            : host(h), port(p), containerID(c) {}
    };

    // Cluster info (static - shared across all tests)
    static std::string clusterID;
    static std::vector<ServerInfo> servers;
    static int numServers;
    static int initialServerCount;  // Number of servers to start in SetUp

    /**
     * Set the number of servers to start in SetUp().
     * Must be called before running tests (e.g., in main()).
     * Default: 3 servers
     */
    static void setInitialServerCount(int count) {
        if (count < 2 || count > 4) {
            throw std::invalid_argument("Initial server count must be between 2 and 4");
        }
        initialServerCount = count;
    }

    void SetUp() override {
        // Clean up any leftover containers from previous runs
        cleanupLeftoverContainers();

        // Start cluster with initial server count
        startCluster(initialServerCount);
    }

    void TearDown() override {
        // Stop entire cluster
        stopCluster();

        // Final cleanup to ensure no containers are left
        cleanupLeftoverContainers();
    }

    /**
     * Clean up any leftover containers from previous test runs.
     * This is called in SetUp() before starting the cluster to ensure a clean state.
     */
    static void cleanupLeftoverContainers() {
        // Remove all ispn-node containers (running or stopped)
        std::string cleanupCmd = "docker rm -f $(docker ps -aq --filter \"name=ispn-node\") 2>/dev/null || true";
        system(cleanupCmd.c_str());

        // Clean up any stale hotrod-test networks
        std::string networkCleanup = "docker network ls --filter \"name=hotrod-test\" -q 2>/dev/null | xargs -r docker network rm 2>/dev/null || true";
        system(networkCleanup.c_str());
    }

    /**
     * Start cluster with specified number of nodes.
     * Called automatically in SetUp(), or can be called manually.
     */
    static void startCluster(int nodeCount) {
        if (nodeCount < 2 || nodeCount > 4) {
            throw std::invalid_argument("Node count must be between 2 and 4");
        }

        // Clear any existing server info
        servers.clear();
        clusterID.clear();

        // Run start_cluster.sh
        std::string cmd = "./scripts/start_cluster.sh " + std::to_string(nodeCount) +
                         " > /tmp/cluster-env.sh 2>&1";
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to start cluster. Check Docker is running and scripts are executable.");
        }

        // Parse cluster info from exported environment variables
        parseClusterInfo("/tmp/cluster-env.sh", nodeCount);

        numServers = static_cast<int>(servers.size());

        if (numServers != nodeCount) {
            throw std::runtime_error("Expected " + std::to_string(nodeCount) +
                                   " servers but got " + std::to_string(numServers));
        }
    }

    /**
     * Stop entire cluster.
     * Called automatically in TearDown(), or can be called manually.
     */
    static void stopCluster() {
        if (clusterID.empty()) {
            return;  // No cluster running
        }

        std::string cmd = "./scripts/stop_cluster.sh " + clusterID;
        system(cmd.c_str());

        servers.clear();
        clusterID.clear();
        numServers = 0;
    }

    /**
     * Dynamically add a node to the running cluster.
     * @param nodeNumber Node number to add (2-4)
     */
    static void addNode(int nodeNumber) {
        if (nodeNumber < 1 || nodeNumber > 4) {
            throw std::invalid_argument("Node number must be between 1 and 4");
        }

        if (clusterID.empty()) {
            throw std::runtime_error("No cluster running");
        }

        // Run add_cluster_node.sh with debug mode
        std::string cmd = "bash -x ./scripts/add_cluster_node.sh " + clusterID + " " +
                         std::to_string(nodeNumber) + " > /tmp/node-add-env.sh 2>&1";

        fprintf(stderr, "[DEBUG] MultiServerTestEnvironment::addNode(%d) - STARTING\n", nodeNumber);
        fprintf(stderr, "[DEBUG]   Cluster ID: %s\n", clusterID.c_str());
        fprintf(stderr, "[DEBUG]   Command: %s\n", cmd.c_str());
        fprintf(stderr, "[DEBUG]   Log file: /tmp/node-add-env.sh\n");

        int result = system(cmd.c_str());

        fprintf(stderr, "[DEBUG] MultiServerTestEnvironment::addNode(%d) - FINISHED\n", nodeNumber);
        fprintf(stderr, "[DEBUG]   Exit code: %d\n", result);

        if (result != 0) {
            fprintf(stderr, "[ERROR] Script failed! Showing /tmp/node-add-env.sh:\n");
            system("cat /tmp/node-add-env.sh >&2");
            throw std::runtime_error("Failed to add node " + std::to_string(nodeNumber));
        }

        // Parse new node info
        ServerInfo newNode = parseNodeInfo("/tmp/node-add-env.sh", nodeNumber);

        // Add to servers vector (ensure proper index)
        fprintf(stderr, "[DEBUG]   Before update: servers.size()=%zu, numServers=%d\n",
                servers.size(), numServers);

        if (nodeNumber - 1 >= static_cast<int>(servers.size())) {
            servers.resize(nodeNumber);
            fprintf(stderr, "[DEBUG]   Resized servers vector to %d\n", nodeNumber);
        }
        servers[nodeNumber - 1] = newNode;

        // Recount active servers (same logic as removeNode)
        numServers = 0;
        for (const auto& server : servers) {
            if (server.port != 0) {
                numServers++;
            }
        }
        fprintf(stderr, "[DEBUG]   After update: servers.size()=%zu, numServers=%d\n",
                servers.size(), numServers);
    }

    /**
     * Remove (stop) a node from the cluster.
     * @param nodeNumber Node number to remove (1-4)
     */
    static void removeNode(int nodeNumber) {
        if (nodeNumber < 1 || nodeNumber > 4) {
            throw std::invalid_argument("Node number must be between 1 and 4");
        }

        if (clusterID.empty()) {
            throw std::runtime_error("No cluster running");
        }

        // Run remove_cluster_node.sh
        std::string cmd = "./scripts/remove_cluster_node.sh " + clusterID + " " +
                         std::to_string(nodeNumber) + " 2>&1";
        int result = system(cmd.c_str());
        if (result != 0) {
            throw std::runtime_error("Failed to remove node " + std::to_string(nodeNumber));
        }

        // Mark node as removed (set port to 0)
        if (nodeNumber - 1 < static_cast<int>(servers.size())) {
            servers[nodeNumber - 1].port = 0;
            servers[nodeNumber - 1].containerID.clear();
        }

        // Recount active servers
        numServers = 0;
        for (const auto& server : servers) {
            if (server.port != 0) {
                numServers++;
            }
        }
    }

    /**
     * Wait for cluster to reach expected size.
     * @param expectedSize Expected number of cluster members
     * @param timeoutSeconds Timeout in seconds (default: 60)
     */
    static void waitForClusterSize(int expectedSize, int timeoutSeconds = 60) {
        if (clusterID.empty()) {
            throw std::runtime_error("No cluster running");
        }

        std::string cmd = "bash -x ./scripts/wait_for_cluster_size.sh " + clusterID + " " +
                         std::to_string(expectedSize) + " 2>&1";

        fprintf(stderr, "[DEBUG] MultiServerTestEnvironment::waitForClusterSize(%d) - STARTING\n", expectedSize);
        fprintf(stderr, "[DEBUG]   Timeout: %d seconds\n", timeoutSeconds);
        fprintf(stderr, "[DEBUG]   Command: %s\n", cmd.c_str());

        // Set timeout
        std::string timeoutCmd = "timeout " + std::to_string(timeoutSeconds) + " " + cmd;

        int result = system(timeoutCmd.c_str());

        fprintf(stderr, "[DEBUG] MultiServerTestEnvironment::waitForClusterSize(%d) - FINISHED\n", expectedSize);
        fprintf(stderr, "[DEBUG]   Exit code: %d\n", result);

        if (result != 0) {
            fprintf(stderr, "[ERROR] Timeout or failure waiting for cluster size %d\n", expectedSize);
            throw std::runtime_error("Timeout waiting for cluster size " +
                                   std::to_string(expectedSize));
        }

        fprintf(stderr, "[DEBUG] Cluster reached size %d successfully!\n", expectedSize);
    }

    /**
     * Get server info by index (0-based).
     */
    static const ServerInfo& getServer(int index) {
        if (index < 0 || index >= static_cast<int>(servers.size())) {
            throw std::out_of_range("Server index out of range: " + std::to_string(index));
        }
        return servers[index];
    }

    /**
     * Check if a node is active.
     */
    static bool isNodeActive(int nodeNumber) {
        if (nodeNumber < 1 || nodeNumber > static_cast<int>(servers.size())) {
            return false;
        }
        return servers[nodeNumber - 1].port != 0;
    }

private:
    /**
     * Parse cluster info from environment file.
     */
    static void parseClusterInfo(const std::string& envFile, int expectedNodes) {
        std::ifstream env(envFile);
        if (!env.is_open()) {
            throw std::runtime_error("Failed to open " + envFile);
        }

        servers.resize(expectedNodes);

        std::string line;
        while (std::getline(env, line)) {
            // Skip non-export lines
            if (line.find("export") != 0) {
                continue;
            }

            // Parse cluster ID
            if (line.find("export ISPN_CLUSTER_ID=") == 0) {
                clusterID = line.substr(23);
                continue;
            }

            // Parse node info
            for (int i = 1; i <= expectedNodes; i++) {
                std::string nodePrefix = "export ISPN_NODE" + std::to_string(i) + "_";

                if (line.find(nodePrefix + "HOST=") == 0) {
                    servers[i - 1].host = line.substr(nodePrefix.length() + 5);
                } else if (line.find(nodePrefix + "PORT=") == 0) {
                    servers[i - 1].port = std::stoi(line.substr(nodePrefix.length() + 5));
                } else if (line.find(nodePrefix + "CONTAINER=") == 0) {
                    servers[i - 1].containerID = line.substr(nodePrefix.length() + 10);
                }
            }
        }

        if (clusterID.empty()) {
            throw std::runtime_error("Failed to parse cluster ID from " + envFile);
        }

        for (int i = 0; i < expectedNodes; i++) {
            if (servers[i].port == 0) {
                throw std::runtime_error("Failed to parse info for node " + std::to_string(i + 1));
            }
        }
    }

    /**
     * Parse single node info from environment file.
     */
    static ServerInfo parseNodeInfo(const std::string& envFile, int nodeNumber) {
        std::ifstream env(envFile);
        if (!env.is_open()) {
            throw std::runtime_error("Failed to open " + envFile);
        }

        ServerInfo node;
        std::string nodePrefix = "export ISPN_NODE" + std::to_string(nodeNumber) + "_";

        std::string line;
        while (std::getline(env, line)) {
            if (line.find("export") != 0) {
                continue;
            }

            if (line.find(nodePrefix + "HOST=") == 0) {
                node.host = line.substr(nodePrefix.length() + 5);
            } else if (line.find(nodePrefix + "PORT=") == 0) {
                node.port = std::stoi(line.substr(nodePrefix.length() + 5));
            } else if (line.find(nodePrefix + "CONTAINER=") == 0) {
                node.containerID = line.substr(nodePrefix.length() + 10);
            }
        }

        if (node.port == 0) {
            throw std::runtime_error("Failed to parse node " + std::to_string(nodeNumber) + " info");
        }

        return node;
    }
};

// Static member initialization
std::string MultiServerTestEnvironment::clusterID;
std::vector<MultiServerTestEnvironment::ServerInfo> MultiServerTestEnvironment::servers;
int MultiServerTestEnvironment::numServers = 0;
int MultiServerTestEnvironment::initialServerCount = 3;  // Default: 3 servers

} // namespace test
} // namespace hotrod
