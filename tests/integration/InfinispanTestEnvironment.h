#pragma once

#include <tuple>
#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

namespace hotrod {
namespace test {

/**
 * Global test environment for Infinispan integration tests.
 *
 * Manages Infinispan server container lifecycle:
 * - SetUp(): Start container once before all tests
 * - TearDown(): Stop container once after all tests
 *
 * Server info (host, port, containerID) stored in static members,
 * accessible from all integration tests.
 */
class InfinispanTestEnvironment : public ::testing::Environment {
public:
    static std::string containerID;
    static std::string host;
    static int port;
    static bool authEnabled;

    void SetUp() override {
        // Run startup script
        int result = system("./scripts/start_infinispan_noauth.sh > /tmp/ispn-env.sh 2>&1");
        if (result != 0) {
            throw std::runtime_error("Failed to start Infinispan server. Check Docker is running.");
        }

        // Parse exported environment variables
        std::ifstream env("/tmp/ispn-env.sh");
        if (!env.is_open()) {
            throw std::runtime_error("Failed to open /tmp/ispn-env.sh");
        }

        std::string line;
        while (std::getline(env, line)) {
            // Skip non-export lines
            if (line.find("export") != 0) {
                continue;
            }

            if (line.find("export ISPN_CONTAINER_ID=") == 0) {
                containerID = line.substr(25);  // "export ISPN_CONTAINER_ID=" is 25 chars
            } else if (line.find("export ISPN_HOST=") == 0) {
                host = line.substr(17);
            } else if (line.find("export ISPN_PORT=") == 0) {
                port = std::stoi(line.substr(17));
            } else if (line.find("export ISPN_AUTH=") == 0) {
                authEnabled = (line.substr(17) == "true");
            }
        }

        if (containerID.empty() || port == 0) {
            throw std::runtime_error("Failed to parse server info from startup script");
        }
    }

    void TearDown() override {
        // Stop and remove container (hide stdout, show stderr)
        std::string cmd = "./scripts/stop_infinispan.sh " + containerID + " >/dev/null";
        std::ignore = system(cmd.c_str());
    }
};

// Static member initialization
std::string InfinispanTestEnvironment::containerID;
std::string InfinispanTestEnvironment::host = "localhost";
int InfinispanTestEnvironment::port = 0;
bool InfinispanTestEnvironment::authEnabled = false;

} // namespace test
} // namespace hotrod
