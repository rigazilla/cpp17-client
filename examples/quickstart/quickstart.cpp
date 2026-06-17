/**
 * Hot Rod C++ Client - Quickstart Example
 *
 * This example demonstrates basic operations:
 * - Connecting to Infinispan server
 * - PUT: Store key-value pairs
 * - GET: Retrieve values by key
 * - REMOVE: Delete entries
 */

#include "hotrod/RemoteCache.h"
#include <iostream>
#include <string>

using namespace hotrod;

int main() {
    std::cout << "=== Hot Rod C++ Client Quickstart ===" << std::endl;

    try {
        // Connect to Infinispan server running on localhost:11222
        // Using the default cache (___defaultcache)
        RemoteCache cache("localhost", 11222);

        std::cout << "Connecting to Infinispan server..." << std::endl;
        cache.connect();
        std::cout << "Connected successfully!" << std::endl;

        // ===== PUT: Store key-value pairs =====
        std::cout << "\n--- PUT Operations ---" << std::endl;

        // Store some entries
        std::string key1 = "greeting";
        std::string value1 = "Hello, Infinispan!";

        std::string key2 = "language";
        std::string value2 = "C++17";

        std::string key3 = "version";
        std::string value3 = "1.0.0";

        ByteArray keyBytes1(key1.begin(), key1.end());
        ByteArray valueBytes1(value1.begin(), value1.end());
        cache.put(keyBytes1, valueBytes1);
        std::cout << "PUT: " << key1 << " = " << value1 << std::endl;

        ByteArray keyBytes2(key2.begin(), key2.end());
        ByteArray valueBytes2(value2.begin(), value2.end());
        cache.put(keyBytes2, valueBytes2);
        std::cout << "PUT: " << key2 << " = " << value2 << std::endl;

        ByteArray keyBytes3(key3.begin(), key3.end());
        ByteArray valueBytes3(value3.begin(), value3.end());
        cache.put(keyBytes3, valueBytes3);
        std::cout << "PUT: " << key3 << " = " << value3 << std::endl;

        // ===== GET: Retrieve values =====
        std::cout << "\n--- GET Operations ---" << std::endl;

        ByteArray retrievedValue;

        // Get existing key
        if (cache.get(keyBytes1, retrievedValue)) {
            std::string valueStr(retrievedValue.begin(), retrievedValue.end());
            std::cout << "GET: " << key1 << " = " << valueStr << std::endl;
        }

        if (cache.get(keyBytes2, retrievedValue)) {
            std::string valueStr(retrievedValue.begin(), retrievedValue.end());
            std::cout << "GET: " << key2 << " = " << valueStr << std::endl;
        }

        if (cache.get(keyBytes3, retrievedValue)) {
            std::string valueStr(retrievedValue.begin(), retrievedValue.end());
            std::cout << "GET: " << key3 << " = " << valueStr << std::endl;
        }

        // Try to get non-existent key
        std::string missingKey = "nonexistent";
        ByteArray missingKeyBytes(missingKey.begin(), missingKey.end());
        if (!cache.get(missingKeyBytes, retrievedValue)) {
            std::cout << "GET: " << missingKey << " = <not found>" << std::endl;
        }

        // ===== REMOVE: Delete entries =====
        std::cout << "\n--- REMOVE Operations ---" << std::endl;

        // Remove an entry
        if (cache.remove(keyBytes2)) {
            std::cout << "REMOVE: " << key2 << " deleted successfully" << std::endl;
        }

        // Verify it's gone
        if (!cache.get(keyBytes2, retrievedValue)) {
            std::cout << "GET: " << key2 << " = <not found (after delete)>" << std::endl;
        }

        // ===== Summary =====
        std::cout << "\n--- Summary ---" << std::endl;
        std::cout << "Remaining entries in cache:" << std::endl;

        if (cache.get(keyBytes1, retrievedValue)) {
            std::string valueStr(retrievedValue.begin(), retrievedValue.end());
            std::cout << "  " << key1 << " = " << valueStr << std::endl;
        }

        if (cache.get(keyBytes3, retrievedValue)) {
            std::string valueStr(retrievedValue.begin(), retrievedValue.end());
            std::cout << "  " << key3 << " = " << valueStr << std::endl;
        }

        // Disconnect
        cache.disconnect();
        std::cout << "\nDisconnected from server." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== Quickstart Complete ===" << std::endl;
    return 0;
}
