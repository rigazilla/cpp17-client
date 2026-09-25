#include <gtest/gtest.h>
#include "TopologyTestFixture.h"
#include "hotrod/RemoteCache.h"
#include "hotrod/HotRodClientException.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>

using namespace hotrod;
using namespace hotrod::test;

/**
 * Step 11c — keyless-op retry, end-to-end (ping).
 *
 * ping() carries no key/owner, so it routes through selectAnyServer(): every
 * server is a candidate in topology order (minus an exclusion set), with the
 * same two-tier model as keyed ops — automatic before-send failover across
 * servers, plus user-decided after-send retry via cache.excluding(e).ping().
 * These tests assert that keyless dispatch on a live cluster:
 *
 *   - excluding({a live server}) still pings successfully (routes elsewhere)
 *   - excluding(all servers) throws BeforeSend with ownersExhausted=FALSE
 *     (the keyless distinction from keyed selectServerForKey, which sets it true)
 *   - the catch → excluding(e).ping() loop recovers after a node is killed
 *
 * See docs/DECISIONS.md (2026-09-25 Step 11c entry) and
 * docs/ERROR_HANDLING_DESIGN.md §3.2 / D2.
 */
class PingRetryIntegrationTest : public TopologyTest {
protected:
    void SetUp() override {
        TopologyTest::SetUp();
        resetCluster(3);
        clearTestCache();
    }

    static ServerAddress addrOf(const ServerInfo& s) {
        return ServerAddress{s.host, s.port};
    }

    // Ping until the client has learned the cluster topology (ping triggers a
    // topology update in its response header, exactly like the keyed ops).
    std::vector<ServerAddress> pingUntilTopologyKnown(RemoteCache* cache) {
        for (int i = 0; i < 20; ++i) {
            cache->ping().get();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto& servers = cache->getTopology().getServers();
            if (servers.size() >= 2u) {
                std::vector<ServerAddress> addrs;
                for (const auto& s : servers) addrs.push_back(addrOf(s));
                return addrs;
            }
        }
        return {};
    }

    // Map a topology address to its cluster node number (1-based). The topology
    // reports internal container addresses (e.g. 172.18.0.x:11222), not the
    // published localhost:1132x ports, so we map by position: the topology is
    // ordered node1, node2, … — the same convention RetryViewIntegrationTest uses.
    int nodeNumberFor(RemoteCache* cache, const ServerAddress& a) {
        const auto& topo = cache->getTopology().getServers();
        for (size_t i = 0; i < topo.size(); ++i) {
            if (addrOf(topo[i]) == a) return static_cast<int>(i) + 1;
        }
        return -1;
    }
};

// Test 1: a plain ping() works across the cluster, and excluding one live server
// still succeeds — selectAnyServer routes the keyless op to another node.
// Deterministic; no node is killed.
TEST_F(PingRetryIntegrationTest, ExcludingRoutesToAnotherServer) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    auto servers = pingUntilTopologyKnown(cache);
    ASSERT_GE(servers.size(), 2u) << "ping must populate a multi-node topology";

    // Plain keyless ping succeeds.
    ASSERT_NO_THROW(cache->ping().get());

    // Excluding the first candidate still pings — routed to a different server.
    const ServerAddress first = servers.front();
    fprintf(stderr, "[TEST] excluding %s:%u, ping must land elsewhere\n",
            first.host.c_str(), first.port);
    ASSERT_NO_THROW(cache->excluding({first}).ping().get());
}

// Test 2: excluding every server leaves selectAnyServer no candidate. It throws a
// transient, BeforeSend HotRodClientException — and ownersExhausted is FALSE,
// because a keyless op has no owners (this is the one behavioural difference from
// the keyed ProxyDisabledOwnersExhaustedThrows case). Deterministic.
TEST_F(PingRetryIntegrationTest, AllServersExcludedThrowsBeforeSend) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    auto servers = pingUntilTopologyKnown(cache);
    ASSERT_GE(servers.size(), 2u);

    bool threw = false;
    try {
        cache->excluding(servers).ping().get();
    } catch (const HotRodClientException& e) {
        threw = true;
        EXPECT_EQ(FailurePhase::BeforeSend, e.phase)
            << "nothing was sent — this is keyless routing exhaustion";
        EXPECT_FALSE(e.ownersExhausted)
            << "keyless ops have no owners; ownersExhausted must stay false";
        EXPECT_TRUE(isTransient(e))
            << "BeforeSend is always retryable (nothing applied)";
        for (const auto& s : servers) {
            EXPECT_NE(std::find(e.triedNodes.begin(), e.triedNodes.end(), s),
                      e.triedNodes.end())
                << "excluded server " << s.host << ":" << s.port
                << " should appear in triedNodes";
        }
    }
    EXPECT_TRUE(threw) << "excluding every server must throw";
}

// Test 3: kill the server the keyless ping targets first, then drive the
// documented catch → excluding(e).ping() loop. It recovers on a surviving node.
// If the first plain ping re-routes automatically (before-send failover) the loop
// simply succeeds on attempt 0; if it throws, we assert the failure is transient
// and that the killed node was recorded as tried and then avoided.
TEST_F(PingRetryIntegrationTest, RecoversAfterFirstCandidateKilled) {
    auto cache = createDefaultClient();
    ASSERT_NE(nullptr, cache);

    auto servers = pingUntilTopologyKnown(cache);
    ASSERT_GE(servers.size(), 2u);

    // selectAnyServer tries the topology in order, so servers.front() is the node
    // the next keyless ping would hit first.
    const ServerAddress target = servers.front();
    const int nodeNum = nodeNumberFor(cache, target);
    ASSERT_NE(-1, nodeNum) << "first ping candidate must map to a cluster node";

    fprintf(stderr, "[TEST] killing first ping candidate node %d (%s:%u)\n",
            nodeNum, target.host.c_str(), target.port);
    removeNode(nodeNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::vector<ServerAddress> excluded;
    bool recovered = false;
    bool sawException = false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        try {
            if (excluded.empty())
                cache->ping().get();
            else
                cache->excluding(excluded).ping().get();
            recovered = true;
            break;
        } catch (const HotRodClientException& e) {
            sawException = true;
            ASSERT_TRUE(isTransient(e))
                << "a killed-node ping failure should be retryable, not futile";
            ASSERT_GT(e.triedNodes.size(), excluded.size())
                << "each failed attempt must widen the exclusion set";
            excluded = e.triedNodes;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    EXPECT_TRUE(recovered) << "the ping retry loop must recover on a surviving node";

    if (sawException) {
        EXPECT_NE(std::find(excluded.begin(), excluded.end(), target),
                  excluded.end())
            << "the killed node should be in the exclusion set we retried with";
        fprintf(stderr, "[PASS] recovered via excluding(e).ping() retry loop\n");
    } else {
        fprintf(stderr, "[PASS] recovered via automatic before-send re-routing\n");
    }

    // Restore the cluster for subsequent tests.
    addNode(nodeNum);
    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// Main — 3-node cluster, matching the other multi-server suites.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    MultiServerTestEnvironment::setInitialServerCount(3);
    ::testing::AddGlobalTestEnvironment(new MultiServerTestEnvironment());
    return RUN_ALL_TESTS();
}
