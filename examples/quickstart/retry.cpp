/**
 * Hot Rod C++ Client - User-Decided Retry Example (Step 11b)
 *
 * The base operations (cache.get/put/...) never retry on their own: a failure
 * surfaces as a typed HotRodClientException carrying the facts you need to decide
 * what to do next (which phase it failed in, which nodes were already tried).
 * You opt into a retry explicitly, on the failure path, via cache.excluding():
 *
 *     try {
 *         value = cache.get(key).get();
 *     } catch (const HotRodClientException& e) {
 *         if (!isTransient(e)) throw;                 // futile — give up
 *         // ... your idempotency decision (see below) ...
 *         value = cache.excluding(e).get(key).get();  // retry, avoiding e.triedNodes
 *     }
 *
 * Why the library does not retry for you: only YOU know whether replaying THIS
 * particular operation is safe. See the two-axis model below and D2/D4 in
 * docs/DECISIONS.md / docs/ERROR_HANDLING_DESIGN.md.
 *
 * IMPORTANT — the proxyToNonOwner flag does NOT let you skip the catch block.
 * ------------------------------------------------------------------------------
 * proxyToNonOwner=true (the default) only widens the *connection-selection*
 * candidate pool for a SINGLE dispatch: if none of a key's owners can be reached
 * *before the request is sent*, selection falls through to another node that
 * proxies to the real owner. But one operation still executes on exactly ONE
 * node. So a failure that happens *after* the request was sent (a dropped
 * connection, a server ERROR, a command timeout) still comes back to you as an
 * exception. The flag changes where the first attempt is *sent*; it does not make
 * retry automatic and does not remove the need to catch and retry yourself.
 */

#include "hotrod/RemoteCache.h"
#include "hotrod/HotRodClientException.h"
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace hotrod;

// A reusable, idempotent-GET retry loop that follows the documented pattern.
//
// Each failed attempt reports the nodes it tried in e.triedNodes; feeding that
// back through cache.excluding(e) makes the next attempt avoid them, so the
// exclusion set grows monotonically until a live node serves the key or we run
// out of places to try. GET is idempotent, so replaying is always safe here — a
// non-idempotent op (see putWithRetry below) needs the extra outcomeUncertain()
// check before it may be replayed.
std::optional<ByteArray> getWithRetry(RemoteCache& cache, const ByteArray& key,
                                      int maxAttempts = 4) {
    std::vector<ServerAddress> excluded;  // nodes tried so far (empty on 1st try)

    for (int attempt = 1; ; ++attempt) {
        try {
            // First attempt is the plain, retry-free call; later attempts opt
            // into exclusion seeded from the previous failure.
            return excluded.empty()
                ? cache.get(key).get()
                : cache.excluding(excluded).get(key).get();
        } catch (const HotRodClientException& e) {
            std::cerr << "  attempt " << attempt << " failed: " << e.what()
                      << "  (tried " << e.triedNodes.size() << " node(s))\n";

            // Axis 2 — is a retry even worthwhile? A permanent, request-level
            // error (bad request, cache not found, ...) will fail identically on
            // every node, so stop.
            if (!isTransient(e)) {
                std::cerr << "  -> permanent failure, not retrying\n";
                throw;
            }
            // No live node left to try (all owners exhausted and no proxy).
            if (attempt >= maxAttempts) {
                std::cerr << "  -> giving up after " << attempt << " attempts\n";
                throw;
            }
            // Carry the tried nodes forward as the next attempt's exclusion set.
            excluded = e.triedNodes;
        }
    }
}

// The same loop for a NON-idempotent write. The extra rule: if the outcome is
// uncertain (the request may have been applied before the failure — an AfterSend
// drop or a command timeout), replaying a put could double-apply. Here we still
// retry because put(key, value) is naturally idempotent (same key+value), but the
// point is that THIS is the decision only the caller can make.
std::optional<EntryWithMetadata> putWithRetry(RemoteCache& cache,
                                              const ByteArray& key,
                                              const ByteArray& value,
                                              int maxAttempts = 4) {
    std::vector<ServerAddress> excluded;

    for (int attempt = 1; ; ++attempt) {
        try {
            return excluded.empty()
                ? cache.put(key, value).get()
                : cache.excluding(excluded).put(key, value).get();
        } catch (const HotRodClientException& e) {
            std::cerr << "  put attempt " << attempt << " failed: " << e.what() << "\n";

            if (!isTransient(e)) throw;

            if (outcomeUncertain(e)) {
                // The write may already have landed on the server. For a plain
                // idempotent overwrite that is fine; for an accumulating or
                // conditional op you might instead verify with getWithMetadata()
                // before replaying. Decide per operation — the library will not.
                std::cerr << "  -> outcome uncertain; safe to replay this "
                             "idempotent put\n";
            }
            if (attempt >= maxAttempts) throw;
            excluded = e.triedNodes;
        }
    }
}

int main() {
    std::cout << "=== Hot Rod C++ Client - User-Decided Retry ===\n";

    try {
        RemoteCache cache("localhost", 11222, "quickstart-cache");

        // Hash-distribution awareness is what gives the client owner-aware
        // routing (and therefore a meaningful set of nodes to exclude on retry).
        cache.setClientIntelligence(ClientIntelligence::HASH_DISTRIBUTION_AWARE);

        // proxyToNonOwner defaults to true; shown here for emphasis. Remember: it
        // only affects where the FIRST attempt is sent — it does not make retry
        // automatic (see the file header).
        cache.setProxyToNonOwner(true);

        std::cout << "Connecting...\n";
        cache.connect();
        std::cout << "Connected.\n\n";

        std::string keyStr = "retry-demo-key";
        std::string valueStr = "retry-demo-value";
        ByteArray key(keyStr.begin(), keyStr.end());
        ByteArray value(valueStr.begin(), valueStr.end());

        std::cout << "--- PUT with retry ---\n";
        putWithRetry(cache, key, value);
        std::cout << "PUT ok: " << keyStr << " = " << valueStr << "\n\n";

        std::cout << "--- GET with retry ---\n";
        auto result = getWithRetry(cache, key);
        if (result.has_value()) {
            std::string got(result->begin(), result->end());
            std::cout << "GET ok: " << keyStr << " = " << got << "\n";
        } else {
            std::cout << "GET: <not found>\n";
        }

        std::cout << "\nTo actually exercise the retry path, run against a cluster\n"
                     "and kill an owner node mid-run: the plain call throws a\n"
                     "HotRodClientException and the loop retries via excluding(e).\n";

        cache.disconnect();
        std::cout << "\nDisconnected.\n";

    } catch (const HotRodClientException& e) {
        // Typed catch: still catchable as std::runtime_error if you prefer.
        std::cerr << "Hot Rod error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n=== Retry Example Complete ===\n";
    return 0;
}
