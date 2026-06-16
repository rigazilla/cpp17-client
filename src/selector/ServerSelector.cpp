#include "hotrod/ServerSelector.h"
#include "hotrod/HeaderCodec.h"
#include <cstdlib>
#include <ctime>

namespace hotrod {

// ============================================================================
// RoundRobinSelector
// ============================================================================

const ServerAddress* RoundRobinSelector::selectServer(
    const std::vector<ServerAddress>& servers,
    const ServerAddress* currentServer) {

    if (servers.empty()) {
        return nullptr;
    }

    // If we have a current server and it's still in the topology, prefer next one
    if (currentServer) {
        // Find current server in the list
        for (size_t i = 0; i < servers.size(); i++) {
            if (servers[i] == *currentServer) {
                // Found current server, select next one (round-robin)
                currentIndex_ = (i + 1) % servers.size();
                return &servers[currentIndex_];
            }
        }
    }

    // Current server not found (removed from topology), or no current server
    // Use round-robin from current index
    const ServerAddress* selected = &servers[currentIndex_];
    currentIndex_ = (currentIndex_ + 1) % servers.size();
    return selected;
}

// ============================================================================
// RandomSelector
// ============================================================================

RandomSelector::RandomSelector() {
    // Seed random number generator
    // Note: Not cryptographically secure, but sufficient for load balancing
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
}

const ServerAddress* RandomSelector::selectServer(
    const std::vector<ServerAddress>& servers,
    const ServerAddress* currentServer) {

    if (servers.empty()) {
        return nullptr;
    }

    if (servers.size() == 1) {
        return &servers[0];
    }

    // Select random server (different from current if possible)
    size_t selectedIndex = std::rand() % servers.size();

    // If we got the same server and there are alternatives, try once more
    if (currentServer && servers[selectedIndex] == *currentServer && servers.size() > 1) {
        selectedIndex = (selectedIndex + 1) % servers.size();
    }

    return &servers[selectedIndex];
}

} // namespace hotrod
