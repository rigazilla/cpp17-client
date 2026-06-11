#include <gtest/gtest.h>
#include "hotrod/ConsistentHash.h"
#include "hotrod/Codec.h"

using namespace hotrod;

// ============================================================================
// Helper Functions
// ============================================================================

// Build a simple hash topology with 4 segments, 2 owners each
ByteArray buildSimpleHashTopology() {
    ByteArray buffer;

    // Number of segments: 4
    Codec::writeVInt(buffer, 4);

    // Number of owners: 2
    buffer.push_back(2);

    // Segment ownership (4 segments × 2 owners = 8 hash IDs)
    // Segment 0: servers 100, 200
    // Segment 1: servers 200, 100
    // Segment 2: servers 100, 300
    // Segment 3: servers 300, 100

    // Segment 0
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(100);  // hashId 100
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(200);  // hashId 200

    // Segment 1
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(200);
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(100);

    // Segment 2
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(100);
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(1); buffer.push_back(44);   // hashId 300

    // Segment 3
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(1); buffer.push_back(44);
    buffer.push_back(0); buffer.push_back(0); buffer.push_back(0); buffer.push_back(100);

    return buffer;
}

TopologyInfo buildSimpleTopology() {
    TopologyInfo topology;
    topology.addServer(ServerInfo("server1", 11222, 100));
    topology.addServer(ServerInfo("server2", 11322, 200));
    topology.addServer(ServerInfo("server3", 11422, 300));
    return topology;
}

// ============================================================================
// Parsing Tests
// ============================================================================

TEST(ConsistentHashTest, ParseSimpleHashTopology) {
    ByteArray buffer = buildSimpleHashTopology();
    TopologyInfo topology = buildSimpleTopology();

    ConsistentHash ch;
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    EXPECT_TRUE(ch.hasHashTopology());
    EXPECT_EQ(ch.getNumSegments(), 4);
    EXPECT_EQ(ch.getNumOwners(), 2);
}

TEST(ConsistentHashTest, ParseInvalidBuffer_TooShort) {
    ByteArray buffer = {1};  // Only numSegments, missing numOwners
    TopologyInfo topology = buildSimpleTopology();

    ConsistentHash ch;
    size_t offset = 0;

    EXPECT_THROW({
        ch.parseHashTopology(buffer, offset, topology);
    }, std::runtime_error);
}

TEST(ConsistentHashTest, ParseInvalidBuffer_MissingHashIds) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 2);  // 2 segments
    buffer.push_back(2);          // 2 owners
    // Missing 4 hash IDs (2 segments × 2 owners)

    TopologyInfo topology = buildSimpleTopology();

    ConsistentHash ch;
    size_t offset = 0;

    EXPECT_THROW({
        ch.parseHashTopology(buffer, offset, topology);
    }, std::runtime_error);
}

// ============================================================================
// Segment Calculation Tests
// ============================================================================

TEST(ConsistentHashTest, GetSegment_BasicKeys) {
    ConsistentHash ch;

    // Create a simple 4-segment topology
    ByteArray buffer = buildSimpleHashTopology();
    TopologyInfo topology = buildSimpleTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    // Test various keys
    ByteArray key1 = {107, 101, 121, 49};  // "key1"
    int seg1 = ch.getSegment(key1);
    EXPECT_GE(seg1, 0);
    EXPECT_LT(seg1, 4);

    ByteArray key2 = {107, 101, 121, 50};  // "key2"
    int seg2 = ch.getSegment(key2);
    EXPECT_GE(seg2, 0);
    EXPECT_LT(seg2, 4);

    // Different keys should (usually) map to different segments
    // (not guaranteed, but very likely with good hash distribution)
}

TEST(ConsistentHashTest, GetSegment_Deterministic) {
    ConsistentHash ch;

    ByteArray buffer = buildSimpleHashTopology();
    TopologyInfo topology = buildSimpleTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    ByteArray key = {116, 101, 115, 116};  // "test"

    // Same key should always map to same segment
    int seg1 = ch.getSegment(key);
    int seg2 = ch.getSegment(key);
    int seg3 = ch.getSegment(key);

    EXPECT_EQ(seg1, seg2);
    EXPECT_EQ(seg2, seg3);
}

TEST(ConsistentHashTest, GetSegment_NoHashTopology) {
    ConsistentHash ch;

    ByteArray key = {116, 101, 115, 116};
    int seg = ch.getSegment(key);

    // Should return 0 when no topology
    EXPECT_EQ(seg, 0);
}

// ============================================================================
// Primary Owner Tests
// ============================================================================

TEST(ConsistentHashTest, GetPrimaryOwner) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray buffer = buildSimpleHashTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    ByteArray key = {107, 101, 121, 49};  // "key1"
    const ServerInfo* owner = ch.getPrimaryOwner(key, topology);

    ASSERT_NE(owner, nullptr);
    // Owner should be one of the 3 servers
    EXPECT_TRUE(owner->host == "server1" ||
                owner->host == "server2" ||
                owner->host == "server3");
}

TEST(ConsistentHashTest, GetPrimaryOwner_NoHashTopology) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray key = {107, 101, 121, 49};
    const ServerInfo* owner = ch.getPrimaryOwner(key, topology);

    EXPECT_EQ(owner, nullptr);
}

TEST(ConsistentHashTest, GetPrimaryOwner_Deterministic) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray buffer = buildSimpleHashTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    ByteArray key = {116, 101, 115, 116};  // "test"

    const ServerInfo* owner1 = ch.getPrimaryOwner(key, topology);
    const ServerInfo* owner2 = ch.getPrimaryOwner(key, topology);
    const ServerInfo* owner3 = ch.getPrimaryOwner(key, topology);

    ASSERT_NE(owner1, nullptr);
    EXPECT_EQ(owner1, owner2);
    EXPECT_EQ(owner2, owner3);
}

// ============================================================================
// All Owners Tests
// ============================================================================

TEST(ConsistentHashTest, GetOwners) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray buffer = buildSimpleHashTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    ByteArray key = {107, 101, 121, 49};  // "key1"
    auto owners = ch.getOwners(key, topology);

    // Should have 2 owners (numOwners = 2)
    EXPECT_EQ(owners.size(), 2);

    // First owner is primary
    const ServerInfo* primary = ch.getPrimaryOwner(key, topology);
    ASSERT_NE(primary, nullptr);
    EXPECT_EQ(owners[0], primary);
}

TEST(ConsistentHashTest, GetOwners_NoHashTopology) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray key = {107, 101, 121, 49};
    auto owners = ch.getOwners(key, topology);

    EXPECT_TRUE(owners.empty());
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST(ConsistentHashTest, Clear) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray buffer = buildSimpleHashTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    EXPECT_TRUE(ch.hasHashTopology());

    ch.clear();

    EXPECT_FALSE(ch.hasHashTopology());
    EXPECT_EQ(ch.getNumSegments(), 0);
    EXPECT_EQ(ch.getNumOwners(), 0);
}

// ============================================================================
// Realistic Scenario Tests
// ============================================================================

TEST(ConsistentHashTest, Realistic256Segments) {
    ConsistentHash ch;

    // Build 256-segment topology (typical production value)
    ByteArray buffer;
    Codec::writeVInt(buffer, 256);  // 256 segments
    buffer.push_back(2);            // 2 owners per segment

    // For simplicity, alternate between hashId 100 and 200
    for (int seg = 0; seg < 256; seg++) {
        int32_t primary = (seg % 2 == 0) ? 100 : 200;
        int32_t backup = (seg % 2 == 0) ? 200 : 100;

        // Primary owner
        buffer.push_back((primary >> 24) & 0xFF);
        buffer.push_back((primary >> 16) & 0xFF);
        buffer.push_back((primary >> 8) & 0xFF);
        buffer.push_back(primary & 0xFF);

        // Backup owner
        buffer.push_back((backup >> 24) & 0xFF);
        buffer.push_back((backup >> 16) & 0xFF);
        buffer.push_back((backup >> 8) & 0xFF);
        buffer.push_back(backup & 0xFF);
    }

    TopologyInfo topology;
    topology.addServer(ServerInfo("node1", 11222, 100));
    topology.addServer(ServerInfo("node2", 11322, 200));

    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    EXPECT_EQ(ch.getNumSegments(), 256);
    EXPECT_EQ(ch.getNumOwners(), 2);

    // Test key routing
    ByteArray key = {117, 115, 101, 114, 58, 49, 50, 51, 52, 53};  // "user:12345"
    int segment = ch.getSegment(key);
    EXPECT_GE(segment, 0);
    EXPECT_LT(segment, 256);

    const ServerInfo* primary = ch.getPrimaryOwner(key, topology);
    ASSERT_NE(primary, nullptr);
    EXPECT_TRUE(primary->host == "node1" || primary->host == "node2");
}

// ============================================================================
// Hash Distribution Tests
// ============================================================================

TEST(ConsistentHashTest, HashDistribution) {
    ConsistentHash ch;
    TopologyInfo topology = buildSimpleTopology();

    ByteArray buffer = buildSimpleHashTopology();
    size_t offset = 0;
    ch.parseHashTopology(buffer, offset, topology);

    // Generate 100 keys and check they're distributed across segments
    std::vector<int> segmentCounts(4, 0);

    for (int i = 0; i < 100; i++) {
        ByteArray key;
        Codec::writeVInt(key, i);

        int segment = ch.getSegment(key);
        segmentCounts[segment]++;
    }

    // Each segment should get some keys (very unlikely all go to one segment)
    int nonZeroSegments = 0;
    for (int count : segmentCounts) {
        if (count > 0) {
            nonZeroSegments++;
        }
    }

    EXPECT_GT(nonZeroSegments, 1);  // At least 2 segments should be used
}
