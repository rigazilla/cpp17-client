#pragma once

#include "HotRodClientException.h"  // ServerAddress
#include <vector>
#include <algorithm>

namespace hotrod {

/**
 * Build the ordered list of servers to try for a key.
 *
 * Owners come first, in owner order (primary, then backups). When
 * @p proxyToNonOwner is true (the default, matching Java), the remaining servers
 * follow as a non-owner *proxy* fallback; when false, only owners are returned
 * so a caller that exhausts them can report ownersExhausted rather than silently
 * proxying (D5). Nodes in @p excludeNodes are dropped, and duplicates are
 * collapsed (a server that is both an owner and present in @p allServers appears
 * once, keeping its owner position).
 *
 * This is pure — no I/O, no client state — so the routing decision can be
 * unit-tested in isolation from connection acquisition. Connection acquisition
 * (and its failures) is the caller's job; see RemoteCache::selectServerForKey.
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.2 / D5 (Step 11b).
 *
 * @param owners      Owners for the key's segment, in preference order.
 * @param allServers  All servers currently in the topology.
 * @param excludeNodes Nodes to skip (e.g. already tried on a prior attempt).
 * @param proxyToNonOwner If true, append non-owner servers as proxy fallback.
 * @return Ordered, de-duplicated candidate list with excluded nodes removed.
 */
inline std::vector<ServerAddress> orderKeyCandidates(
    const std::vector<ServerAddress>& owners,
    const std::vector<ServerAddress>& allServers,
    const std::vector<ServerAddress>& excludeNodes,
    bool proxyToNonOwner = true)
{
    std::vector<ServerAddress> out;
    out.reserve(owners.size() + allServers.size());

    auto isExcluded = [&](const ServerAddress& s) {
        return std::find(excludeNodes.begin(), excludeNodes.end(), s) != excludeNodes.end();
    };
    auto pushUnique = [&](const ServerAddress& s) {
        if (isExcluded(s)) return;
        if (std::find(out.begin(), out.end(), s) != out.end()) return;
        out.push_back(s);
    };

    for (const auto& o : owners) pushUnique(o);        // owners first, in order
    if (proxyToNonOwner)
        for (const auto& s : allServers) pushUnique(s);  // then non-owner fallback
    return out;
}

/**
 * Set-union of two node lists: the members of @p a in order, followed by the
 * members of @p b not already present. Used to accumulate an operation's
 * triedNodes across successive retries — the exclusion set carried in from prior
 * attempts, unioned with the nodes the current attempt touched — so feeding a
 * caught exception's triedNodes back as the next attempt's exclusion set grows
 * monotonically without duplicates (Step 11b).
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.1 (triedNodes union across retries).
 */
inline std::vector<ServerAddress> unionNodes(std::vector<ServerAddress> a,
                                             const std::vector<ServerAddress>& b)
{
    for (const auto& n : b)
        if (std::find(a.begin(), a.end(), n) == a.end())
            a.push_back(n);
    return a;
}

} // namespace hotrod
