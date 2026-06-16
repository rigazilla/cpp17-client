#pragma once

#include "Types.h"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace hotrod {

// Forward declare ServerAddress (defined in HeaderCodec.h)
struct ServerAddress;

/**
 * Abstract base class for server selection strategies.
 *
 * Allows users to plug in custom load balancing algorithms.
 *
 * Built-in strategies:
 * - RoundRobinSelector: Distribute requests evenly across servers
 * - RandomSelector: Random server selection
 * - (Future) WeightedSelector: Weighted round-robin
 * - (Future) LeastConnectionsSelector: Choose least busy server
 */
class ServerSelector {
public:
    virtual ~ServerSelector() = default;

    /**
     * Select next server from the available list.
     *
     * @param servers List of available servers
     * @param currentServer Currently connected server (may be null)
     * @return Pointer to selected server, or nullptr if no servers available
     */
    virtual const ServerAddress* selectServer(
        const std::vector<ServerAddress>& servers,
        const ServerAddress* currentServer) = 0;

    /**
     * Reset selector state (e.g., when topology changes).
     */
    virtual void reset() = 0;
};

/**
 * Round-robin server selector.
 *
 * Distributes requests evenly across all available servers.
 * Thread-safe for single RemoteCache instance.
 */
class RoundRobinSelector : public ServerSelector {
public:
    RoundRobinSelector() : currentIndex_(0) {}

    const ServerAddress* selectServer(
        const std::vector<ServerAddress>& servers,
        const ServerAddress* currentServer) override;

    void reset() override {
        currentIndex_ = 0;
    }

private:
    size_t currentIndex_;
};

/**
 * Random server selector.
 *
 * Selects a random server from available servers.
 * Provides better distribution under concurrent load than round-robin.
 */
class RandomSelector : public ServerSelector {
public:
    RandomSelector();

    const ServerAddress* selectServer(
        const std::vector<ServerAddress>& servers,
        const ServerAddress* currentServer) override;

    void reset() override {
        // No state to reset for random selection
    }

private:
    // Random seed initialized in constructor
};

} // namespace hotrod
