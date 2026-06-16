#include "hotrod/RemoteCache.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace hotrod;

int main() {
    try {
        // Create client (will use pool)
        RemoteCache cache("localhost", 11222);
        cache.setCacheName("");
        cache.setClientIntelligence(ClientIntelligence::TOPOLOGY_AWARE);

        // Connect
        std::cout << "Connecting to localhost:11222..." << std::endl;
        cache.connect();

        // Initial PING to get topology
        std::cout << "Sending PING to get topology..." << std::endl;
        cache.ping();

        std::cout << "\nTopology received: " << cache.getTopology().servers.size() << " servers" << std::endl;
        for (const auto& server : cache.getTopology().servers) {
            std::cout << "  • " << server.host << ":" << server.port << std::endl;
        }

        // Do 15 operations - watch for pool messages
        std::cout << "\n=== Starting 15 operations (watch for pool messages) ===" << std::endl;
        for (int i = 0; i < 15; i++) {
            std::string keyStr = "test-key-" + std::to_string(i);
            ByteArray key(keyStr.begin(), keyStr.end());
            ByteArray value = {(uint8_t)i};

            cache.put(key, value);
            std::cout << "Operation " << (i+1) << "/15 completed" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "\n✓ All 15 operations completed successfully!" << std::endl;
        std::cout << "\nExpected pattern in logs:" << std::endl;
        std::cout << "  - First 3 operations: '[POOL] Creating new connection'" << std::endl;
        std::cout << "  - Remaining 12 operations: '[POOL] Reusing existing connection'" << std::endl;

        cache.disconnect();
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
