#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

namespace hotrod {
namespace test {

/**
 * Helper utilities for querying Infinispan REST API.
 *
 * Used in integration tests to verify server-side state:
 * - Key counts per server
 * - Key distribution
 * - Server statistics
 *
 * Uses curl commands via system() calls.
 */
class RestAPIHelper {
public:
    /**
     * Get number of keys in a cache on a specific server.
     *
     * REST API v3: GET /rest/v3/caches/{cacheName}/_size
     *
     * @param host Server hostname
     * @param port Server REST port (usually 11222)
     * @param cacheName Cache name (empty = default cache)
     * @return Number of keys, or -1 on error
     */
    static int getKeyCount(const std::string& host, int port, const std::string& cacheName = "") {
        // For Infinispan, empty cache name means "___defaultcache" (3 underscores)
        std::string cache = cacheName.empty() ? "___defaultcache" : cacheName;

        // REST API v3: GET /rest/v3/caches/{cacheName}/_size returns integer directly
        std::string url = "http://" + host + ":" + std::to_string(port) +
                         "/rest/v3/caches/" + cache + "/_size";

        std::string cmd = "curl -s -u admin:password \"" + url + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return -1;

        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        // Result should be a number (returned directly, not as JSON)
        try {
            return std::stoi(result);
        } catch (...) {
            return -1;
        }
    }

    /**
     * Get list of all keys in a cache on a specific server.
     *
     * REST API v3: GET /rest/v3/caches/{cacheName}/keys
     *
     * @param host Server hostname
     * @param port Server REST port
     * @param cacheName Cache name
     * @return Vector of key names (as strings)
     */
    static std::vector<std::string> getKeys(const std::string& host, int port,
                                           const std::string& cacheName = "") {
        std::string cache = cacheName.empty() ? "___defaultcache" : cacheName;

        std::string url = "http://" + host + ":" + std::to_string(port) +
                         "/rest/v3/caches/" + cache + "/keys";

        std::string cmd = "curl -s -u admin:password \"" + url + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return {};

        std::vector<std::string> keys;
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            // Parse JSON array of keys (simple parsing)
            std::string line(buffer);
            // Remove brackets and quotes, split by comma
            // This is simplified - assumes keys don't contain special chars
            // Real implementation should use JSON parser

            // For now, just count lines that look like keys
            if (line.find("\"") != std::string::npos) {
                keys.push_back(line);
            }
        }
        pclose(pipe);

        return keys;
    }

    /**
     * Clear all entries in a cache on a specific server.
     *
     * REST API v3: POST /rest/v3/caches/{cacheName}/_clear
     *
     * @param host Server hostname
     * @param port Server REST port
     * @param cacheName Cache name
     * @return true if successful
     */
    static bool clearCache(const std::string& host, int port, const std::string& cacheName = "") {
        std::string cache = cacheName.empty() ? "___defaultcache" : cacheName;

        std::string url = "http://" + host + ":" + std::to_string(port) +
                         "/rest/v3/caches/" + cache + "/_clear";

        std::string cmd = "curl -s -X POST -u admin:password \"" + url + "\" >/dev/null 2>&1";

        int result = system(cmd.c_str());
        return result == 0;
    }

    /**
     * Get server statistics (hits, misses, stores, etc.).
     *
     * REST API v3: GET /rest/v3/caches/{cacheName}/details (includes stats)
     *
     * @param host Server hostname
     * @param port Server REST port
     * @param cacheName Cache name
     * @return Statistics as string (JSON format)
     */
    static std::string getStats(const std::string& host, int port, const std::string& cacheName = "") {
        std::string cache = cacheName.empty() ? "___defaultcache" : cacheName;

        std::string url = "http://" + host + ":" + std::to_string(port) +
                         "/rest/v3/caches/" + cache + "/details";

        std::string cmd = "curl -s -u admin:password \"" + url + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "";

        std::string result;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);

        return result;
    }

    /**
     * Extract a numeric stat from stats JSON.
     *
     * Simple JSON parsing - looks for "field_name":value pattern.
     *
     * @param stats Stats JSON string
     * @param fieldName Field to extract (e.g., "hits", "stores")
     * @return Field value, or -1 if not found
     */
    static int extractStat(const std::string& stats, const std::string& fieldName) {
        std::string pattern = "\"" + fieldName + "\":";
        size_t pos = stats.find(pattern);
        if (pos == std::string::npos) return -1;

        pos += pattern.length();

        // Skip whitespace
        while (pos < stats.length() && (stats[pos] == ' ' || stats[pos] == '\t')) {
            pos++;
        }

        // Extract number
        std::string numStr;
        while (pos < stats.length() && (isdigit(stats[pos]) || stats[pos] == '-')) {
            numStr += stats[pos++];
        }

        try {
            return std::stoi(numStr);
        } catch (...) {
            return -1;
        }
    }

    /**
     * Put a key-value pair via REST API (for testing).
     *
     * REST API v3: POST /rest/v3/caches/{cacheName}/entries/{cacheKey}
     *
     * @param host Server hostname
     * @param port Server REST port
     * @param cacheName Cache name
     * @param key Key (URL-encoded)
     * @param value Value
     * @return true if successful
     */
    static bool putViaREST(const std::string& host, int port,
                          const std::string& cacheName,
                          const std::string& key, const std::string& value) {
        std::string cache = cacheName.empty() ? "___defaultcache" : cacheName;

        std::string url = "http://" + host + ":" + std::to_string(port) +
                         "/rest/v3/caches/" + cache + "/entries/" + key;

        std::string cmd = "curl -s -X POST -u admin:password "
                         "-H 'Content-Type: text/plain' "
                         "-d '" + value + "' \"" + url + "\" >/dev/null 2>&1";

        int result = system(cmd.c_str());
        return result == 0;
    }
};

} // namespace test
} // namespace hotrod
