#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include "hotrod/RemoteCache.h"
#include "hotrod/HotRodClientException.h"
#include <thread>
#include <chrono>
#include <optional>
#include <vector>
#include <string>
#include <algorithm>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Step 11b — user-decided retry, end-to-end.
 *
 * These tests exercise the *RetryView* dispatch path — the bound view returned by
 * RemoteCache::excluding() — which is how a caller retries an operation while
 * avoiding the nodes a prior attempt already tried. They are deliberately
 * distinct from the pre-existing failover tests (ConcurrentMultiServerTest
 * .ConcurrentWithFailover, HashAwareRoutingIntegrationTest.FailoverToBackupOwner),
 * which cover the *automatic* before-send re-routing that happens on the next
 * plain call. Here we assert the explicit, user-driven exclusion API:
 *
 *   - excluding({owner}) routes the request around an excluded node (deterministic)
 *   - proxyToNonOwner=false + all owners excluded throws ownersExhausted (deterministic)
 *   - the documented catch → excluding(e) retry loop recovers after a node is killed
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.2 / D2 / D5 and docs/DECISIONS.md.
 */
class RetryViewIntegrationTest : public TopologyTest {
protected:
    void SetUp() override {
        TopologyTest::SetUp();
        resetCluster(3);          // hash-aware routing needs a real cluster (numOwners=2)
        clearTestCache();
    }

    // Build the pure-value address of a ServerInfo* owner, matching exactly what
    // orderKeyCandidates()/the exclusion set compare on (host + port).
    static ServerAddress addrOf(const ServerInfo* s) {
        return ServerAddress{s->host, s->port};
    }
};

// Test 1: excluding({primary owner}) still fetches the value by routing to a
// different node (a backup owner, or a non-owner proxy). Deterministic — no node
// is killed; this isolates the RetryView routing decision from failure timing.
TEST_F(RetryViewIntegrationTest, ExcludingRoutesAroundOwner) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    std::this_thread::sleep_for(std::chrono::seconds(2));  // let topology arrive

    std::string keyStr = "retryview-around-owner";
    std::string valueStr = "retryview-around-owner-value";
    ByteArray key(keyStr.begin(), keyStr.end());
    ByteArray value(valueStr.begin(), valueStr.end());

    cache->put(key, value).get();
    std::this_thread::sleep_for(std::chrono::seconds(2));  // replicate to backup

    ASSERT_TRUE(cache->getConsistentHash().hasHashTopology())
        << "hash-aware routing must be active for this test";

    auto owners = cache->getConsistentHash().getOwners(key, cache->getTopology());
    ASSERT_GE(owners.size(), 2u) << "DIST_SYNC with numOwners=2 expected";

    const ServerAddress primary = addrOf(owners[0]);
    fprintf(stderr, "[TEST] excluding primary owner %s:%u\n",
            primary.host.c_str(), primary.port);

    // Route around the primary explicitly via the RetryView.
    auto result = cache->excluding({primary}).get(key).get();

    ASSERT_TRUE(result.has_value())
        << "value must still be reachable via a backup owner / proxy";
    EXPECT_EQ(value, *result);
}

// Test 2: with proxy fallback disabled, excluding every owner leaves no candidate
// and selection throws a transient, BeforeSend HotRodClientException with
// ownersExhausted=true — deterministic, no node killed. This is the D5 contract:
// the library refuses to silently proxy and hands the decision back to the caller.
TEST_F(RetryViewIntegrationTest, ProxyDisabledOwnersExhaustedThrows) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string keyStr = "retryview-owners-exhausted";
    std::string valueStr = "retryview-owners-exhausted-value";
    ByteArray key(keyStr.begin(), keyStr.end());
    ByteArray value(valueStr.begin(), valueStr.end());

    cache->put(key, value).get();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ASSERT_TRUE(cache->getConsistentHash().hasHashTopology());

    auto owners = cache->getConsistentHash().getOwners(key, cache->getTopology());
    ASSERT_GE(owners.size(), 1u);

    std::vector<ServerAddress> allOwners;
    for (const auto* o : owners) allOwners.push_back(addrOf(o));

    cache->setProxyToNonOwner(false);  // no non-owner fallback

    bool threw = false;
    try {
        cache->excluding(allOwners).get(key).get();
    } catch (const HotRodClientException& e) {
        threw = true;
        EXPECT_EQ(FailurePhase::BeforeSend, e.phase)
            << "nothing was sent — this is a routing exhaustion";
        EXPECT_TRUE(e.ownersExhausted)
            << "every owner was excluded, so owners are exhausted";
        EXPECT_TRUE(isTransient(e))
            << "BeforeSend is always retryable (nothing applied)";
        // triedNodes carries the excluded owners forward for the next attempt.
        for (const auto& o : allOwners) {
            EXPECT_NE(std::find(e.triedNodes.begin(), e.triedNodes.end(), o),
                      e.triedNodes.end())
                << "excluded owner " << o.host << ":" << o.port
                << " should appear in triedNodes";
        }
    }
    EXPECT_TRUE(threw) << "excluding all owners with proxy disabled must throw";

    cache->setProxyToNonOwner(true);  // restore default
}

// Test 3: the documented user retry loop. Kill the primary owner, then drive the
// catch → excluding(e) loop. It recovers to a live node holding the value. If the
// first plain GET happens to re-route automatically (before-send failover), the
// loop simply succeeds on attempt 0; if it throws, we assert the failure is
// transient and that feeding e.triedNodes back lands the retry on the backup.
TEST_F(RetryViewIntegrationTest, UserRetryLoopAfterPrimaryKilled) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string keyStr = "retryview-kill-primary";
    std::string valueStr = "retryview-kill-primary-value";
    ByteArray key(keyStr.begin(), keyStr.end());
    ByteArray value(valueStr.begin(), valueStr.end());

    cache->put(key, value).get();
    std::this_thread::sleep_for(std::chrono::seconds(2));  // replicate to backup

    ASSERT_TRUE(cache->getConsistentHash().hasHashTopology());

    const ServerInfo* primaryOwner =
        cache->getConsistentHash().getPrimaryOwner(key, cache->getTopology());
    ASSERT_NE(nullptr, primaryOwner);

    // Map the primary owner to its node number (topology is ordered node1,2,3…).
    const auto& topoServers = cache->getTopology().getServers();
    int primaryNodeNum = -1;
    for (size_t i = 0; i < topoServers.size(); ++i) {
        if (topoServers[i].hashId == primaryOwner->hashId) {
            primaryNodeNum = static_cast<int>(i) + 1;
            break;
        }
    }
    ASSERT_NE(-1, primaryNodeNum) << "primary owner must be found in topology";

    fprintf(stderr, "[TEST] killing primary owner node %d (%s:%u)\n",
            primaryNodeNum, primaryOwner->host.c_str(), primaryOwner->port);
    removeNode(primaryNodeNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Canonical user retry loop (ERROR_HANDLING_DESIGN §3.2): retry-free first
    // call, then opt into exclusion seeded from the caught exception.
    std::optional<ByteArray> result;
    std::vector<ServerAddress> excluded;
    bool sawException = false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        try {
            result = excluded.empty()
                ? cache->get(key).get()
                : cache->excluding(excluded).get(key).get();
            break;  // success — either directly or via the exclusion retry
        } catch (const HotRodClientException& e) {
            sawException = true;
            ASSERT_TRUE(isTransient(e))
                << "a killed-node failure should be retryable, not futile";
            // GET is idempotent, so replaying is always safe here; a caller with a
            // non-idempotent op would additionally consult outcomeUncertain(e).
            ASSERT_GT(e.triedNodes.size(), excluded.size())
                << "each failed attempt must widen the exclusion set";
            excluded = e.triedNodes;  // feed tried nodes back as next exclusion
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    ASSERT_TRUE(result.has_value())
        << "the retry loop must recover the value from a surviving owner";
    EXPECT_EQ(value, *result);

    if (sawException) {
        // If we went through the catch path, the killed primary must be recorded
        // as a tried node the retry then avoided.
        const ServerAddress killed = addrOf(primaryOwner);
        EXPECT_NE(std::find(excluded.begin(), excluded.end(), killed),
                  excluded.end())
            << "the killed primary should be in the exclusion set we retried with";
        fprintf(stderr, "[PASS] recovered via excluding(e) retry loop\n");
    } else {
        fprintf(stderr, "[PASS] recovered via automatic before-send re-routing\n");
    }

    // Restore the cluster for subsequent tests.
    addNode(primaryNodeNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// Main — 3-node cluster, matching the other multi-server suites.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    MultiServerTestEnvironment::setInitialServerCount(3);
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());
    return RUN_ALL_TESTS();
}
