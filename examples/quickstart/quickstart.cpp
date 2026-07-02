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
        // Using the quickstart cache
        RemoteCache cache("localhost", 11222, "quickstart-cache");

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
        cache.put(keyBytes1, valueBytes1).get();  // .get() waits for completion
        std::cout << "PUT: " << key1 << " = " << value1 << std::endl;

        ByteArray keyBytes2(key2.begin(), key2.end());
        ByteArray valueBytes2(value2.begin(), value2.end());
        cache.put(keyBytes2, valueBytes2).get();
        std::cout << "PUT: " << key2 << " = " << value2 << std::endl;

        ByteArray keyBytes3(key3.begin(), key3.end());
        ByteArray valueBytes3(value3.begin(), value3.end());
        cache.put(keyBytes3, valueBytes3).get();
        std::cout << "PUT: " << key3 << " = " << value3 << std::endl;

        // ===== GET: Retrieve values =====
        std::cout << "\n--- GET Operations ---" << std::endl;

        // Get existing key
        auto result1 = cache.get(keyBytes1).get();
        if (result1.has_value()) {
            std::string valueStr(result1.value().begin(), result1.value().end());
            std::cout << "GET: " << key1 << " = " << valueStr << std::endl;
        }

        auto result2 = cache.get(keyBytes2).get();
        if (result2.has_value()) {
            std::string valueStr(result2.value().begin(), result2.value().end());
            std::cout << "GET: " << key2 << " = " << valueStr << std::endl;
        }

        auto result3 = cache.get(keyBytes3).get();
        if (result3.has_value()) {
            std::string valueStr(result3.value().begin(), result3.value().end());
            std::cout << "GET: " << key3 << " = " << valueStr << std::endl;
        }

        // Try to get non-existent key
        std::string missingKey = "nonexistent";
        ByteArray missingKeyBytes(missingKey.begin(), missingKey.end());
        auto missingResult = cache.get(missingKeyBytes).get();
        if (!missingResult.has_value()) {
            std::cout << "GET: " << missingKey << " = <not found>" << std::endl;
        }

        // ===== REMOVE: Delete entries =====
        std::cout << "\n--- REMOVE Operations ---" << std::endl;

        // Remove an entry
        cache.remove(keyBytes2).get();
        std::cout << "REMOVE: " << key2 << " deleted successfully" << std::endl;

        // Verify it's gone
        auto verifyResult = cache.get(keyBytes2).get();
        if (!verifyResult.has_value()) {
            std::cout << "GET: " << key2 << " = <not found (after delete)>" << std::endl;
        }

        // ===== Summary =====
        std::cout << "\n--- Summary ---" << std::endl;
        std::cout << "Remaining entries in cache:" << std::endl;

        auto summary1 = cache.get(keyBytes1).get();
        if (summary1.has_value()) {
            std::string valueStr(summary1.value().begin(), summary1.value().end());
            std::cout << "  " << key1 << " = " << valueStr << std::endl;
        }

        auto summary3 = cache.get(keyBytes3).get();
        if (summary3.has_value()) {
            std::string valueStr(summary3.value().begin(), summary3.value().end());
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
