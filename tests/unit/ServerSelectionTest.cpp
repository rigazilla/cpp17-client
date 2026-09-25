#include <gtest/gtest.h>
#include "hotrod/ServerSelection.h"
#include <vector>

using namespace hotrod;

/**
 * Unit tests for orderKeyCandidates() — the pure routing decision behind
 * exclusion-aware selectServerForKey (Step 11b).
 *
 * It has no I/O, so the ordering/exclusion/dedup behaviour is tested directly,
 * independent of connection acquisition.
 *
 * See docs/ERROR_HANDLING_DESIGN.md §3.2.
 */

namespace {
ServerAddress addr(const std::string& host, uint16_t port) { return {host, port}; }

std::vector<std::string> asKeys(const std::vector<ServerAddress>& v) {
    std::vector<std::string> out;
    for (const auto& a : v) out.push_back(a.host + ":" + std::to_string(a.port));
    return out;
}
} // namespace

// Owners come first, in order, then non-owner servers as proxy fallback.
TEST(ServerSelectionTest, OwnersFirstThenNonOwnerFallback) {
    std::vector<ServerAddress> owners = {addr("a", 1), addr("b", 2)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3), addr("d", 4)};

    auto out = orderKeyCandidates(owners, all, /*exclude=*/{});

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1", "b:2", "c:3", "d:4"}));
}

// A server that is both an owner and in allServers appears once, in owner order.
TEST(ServerSelectionTest, DeduplicatesOwnersAgainstAllServers) {
    std::vector<ServerAddress> owners = {addr("b", 2)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, {});

    // b:2 kept at its owner position (front), not duplicated when seen in `all`.
    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"b:2", "a:1", "c:3"}));
}

// Excluded nodes are dropped whether they are owners or non-owners.
TEST(ServerSelectionTest, ExcludesTriedNodes) {
    std::vector<ServerAddress> owners = {addr("a", 1), addr("b", 2)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, /*exclude=*/{addr("a", 1), addr("c", 3)});

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"b:2"}));
}

// Excluding every candidate yields an empty list (caller treats as exhausted).
TEST(ServerSelectionTest, AllExcludedYieldsEmpty) {
    std::vector<ServerAddress> owners = {addr("a", 1)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2)};

    auto out = orderKeyCandidates(owners, all, {addr("a", 1), addr("b", 2)});

    EXPECT_TRUE(out.empty());
}

// Port distinguishes servers on the same host.
TEST(ServerSelectionTest, SameHostDifferentPortAreDistinct) {
    std::vector<ServerAddress> owners = {addr("h", 11222)};
    std::vector<ServerAddress> all = {addr("h", 11222), addr("h", 11322)};

    auto out = orderKeyCandidates(owners, all, {addr("h", 11222)});

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"h:11322"}));
}

// A non-owner fallback still appears when all owners are excluded.
TEST(ServerSelectionTest, NonOwnerFallbackWhenOwnersExcluded) {
    std::vector<ServerAddress> owners = {addr("a", 1)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, {addr("a", 1)});

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"b:2", "c:3"}));
}

// Empty topology yields no candidates even with owners listed.
TEST(ServerSelectionTest, EmptyAllServersKeepsOwnersOnly) {
    std::vector<ServerAddress> owners = {addr("a", 1), addr("b", 2)};

    auto out = orderKeyCandidates(owners, /*all=*/{}, {});

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1", "b:2"}));
}

// proxyToNonOwner=true (default) appends non-owner servers after the owners.
TEST(ServerSelectionTest, ProxyToNonOwnerTrueAppendsNonOwners) {
    std::vector<ServerAddress> owners = {addr("a", 1)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, /*exclude=*/{}, /*proxyToNonOwner=*/true);

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1", "b:2", "c:3"}));
}

// proxyToNonOwner=false stops at the owners — no non-owner fallback.
TEST(ServerSelectionTest, ProxyToNonOwnerFalseKeepsOwnersOnly) {
    std::vector<ServerAddress> owners = {addr("a", 1)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, /*exclude=*/{}, /*proxyToNonOwner=*/false);

    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1"}));
}

// proxyToNonOwner=false with every owner excluded yields no candidates — the
// caller then reports ownersExhausted rather than proxying.
TEST(ServerSelectionTest, ProxyToNonOwnerFalseAllOwnersExcludedYieldsEmpty) {
    std::vector<ServerAddress> owners = {addr("a", 1), addr("b", 2)};
    std::vector<ServerAddress> all = {addr("a", 1), addr("b", 2), addr("c", 3)};

    auto out = orderKeyCandidates(owners, all, /*exclude=*/{addr("a", 1), addr("b", 2)},
                                  /*proxyToNonOwner=*/false);

    EXPECT_TRUE(out.empty());
}

// --- unionNodes: triedNodes accumulation across retries (Step 11b) ---

// Union appends only new members, preserving the first list's order.
TEST(ServerSelectionTest, UnionAppendsNewNodesPreservingOrder) {
    auto out = unionNodes({addr("a", 1), addr("b", 2)}, {addr("c", 3)});
    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1", "b:2", "c:3"}));
}

// Members already present are not duplicated.
TEST(ServerSelectionTest, UnionDeduplicates) {
    auto out = unionNodes({addr("a", 1), addr("b", 2)}, {addr("b", 2), addr("c", 3)});
    EXPECT_EQ(asKeys(out), (std::vector<std::string>{"a:1", "b:2", "c:3"}));
}

// Empty operands are handled both ways.
TEST(ServerSelectionTest, UnionWithEmptyOperands) {
    EXPECT_EQ(asKeys(unionNodes({}, {addr("a", 1)})), (std::vector<std::string>{"a:1"}));
    EXPECT_EQ(asKeys(unionNodes({addr("a", 1)}, {})), (std::vector<std::string>{"a:1"}));
    EXPECT_TRUE(unionNodes({}, {}).empty());
}

// Folding successive attempts' tried-sets grows monotonically without dupes —
// exactly the accumulation a user's retry loop performs (excludeNodes grows).
TEST(ServerSelectionTest, UnionAccumulatesAcrossRetries) {
    std::vector<ServerAddress> excluded;                       // attempt 1: nothing excluded
    excluded = unionNodes(excluded, {addr("a", 1)});           // attempt 1 tried a
    EXPECT_EQ(asKeys(excluded), (std::vector<std::string>{"a:1"}));

    excluded = unionNodes(excluded, {addr("a", 1), addr("b", 2)}); // attempt 2 tried a(skip)+b
    EXPECT_EQ(asKeys(excluded), (std::vector<std::string>{"a:1", "b:2"}));

    excluded = unionNodes(excluded, {addr("c", 3)});          // attempt 3 tried c
    EXPECT_EQ(asKeys(excluded), (std::vector<std::string>{"a:1", "b:2", "c:3"}));
}
