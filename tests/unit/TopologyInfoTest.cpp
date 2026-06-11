#include <gtest/gtest.h>
#include "hotrod/TopologyInfo.h"
#include "hotrod/Codec.h"

using namespace hotrod;

// ============================================================================
// Topology Parsing Tests
// ============================================================================

// Test parsing a single-server topology
TEST(TopologyInfoTest, ParseSingleServer) {
    // Build topology update buffer
    // Topology ID: 1 (vInt)
    // Server count: 1 (vInt)
    // Server: "localhost", port 11222, hash ID 12345
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);  // Topology ID
    Codec::writeVInt(buffer, 1);  // Server count
    Codec::writeString(buffer, "localhost");  // Hostname
    // Port: 11222 (0x2BD6) - big-endian
    buffer.push_back(0x2B);
    buffer.push_back(0xD6);
    // Hash ID: 12345 (0x00003039) - big-endian
    buffer.push_back(0x00);
    buffer.push_back(0x00);
    buffer.push_back(0x30);
    buffer.push_back(0x39);

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer, offset);

    EXPECT_EQ(topology.getTopologyId(), 1);
    EXPECT_EQ(topology.getServerCount(), 1);

    const auto& servers = topology.getServers();
    ASSERT_EQ(servers.size(), 1);
    EXPECT_EQ(servers[0].host, "localhost");
    EXPECT_EQ(servers[0].port, 11222);
    EXPECT_EQ(servers[0].hashId, 12345);
}

// Test parsing multi-server topology
TEST(TopologyInfoTest, ParseMultiServer) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 5);  // Topology ID 5
    Codec::writeVInt(buffer, 3);  // 3 servers

    // Server 1: node1:11222, hash 100
    Codec::writeString(buffer, "node1");
    buffer.push_back(0x2B); buffer.push_back(0xD6);  // port 11222
    buffer.push_back(0x00); buffer.push_back(0x00);
    buffer.push_back(0x00); buffer.push_back(0x64);  // hash 100

    // Server 2: node2:11322, hash 200
    Codec::writeString(buffer, "node2");
    buffer.push_back(0x2C); buffer.push_back(0x3A);  // port 11322
    buffer.push_back(0x00); buffer.push_back(0x00);
    buffer.push_back(0x00); buffer.push_back(0xC8);  // hash 200

    // Server 3: node3:11422, hash 300
    Codec::writeString(buffer, "node3");
    buffer.push_back(0x2C); buffer.push_back(0x9E);  // port 11422
    buffer.push_back(0x00); buffer.push_back(0x00);
    buffer.push_back(0x01); buffer.push_back(0x2C);  // hash 300

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer, offset);

    EXPECT_EQ(topology.getTopologyId(), 5);
    EXPECT_EQ(topology.getServerCount(), 3);

    const auto& servers = topology.getServers();
    EXPECT_EQ(servers[0].host, "node1");
    EXPECT_EQ(servers[0].port, 11222);
    EXPECT_EQ(servers[1].host, "node2");
    EXPECT_EQ(servers[1].port, 11322);
    EXPECT_EQ(servers[2].host, "node3");
    EXPECT_EQ(servers[2].port, 11422);
}

// Test negative hash ID (signed int32)
TEST(TopologyInfoTest, ParseNegativeHashId) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);
    Codec::writeVInt(buffer, 1);
    Codec::writeString(buffer, "server");
    buffer.push_back(0x2B); buffer.push_back(0xD6);  // port 11222
    // Hash ID: -1 (0xFFFFFFFF) - big-endian
    buffer.push_back(0xFF);
    buffer.push_back(0xFF);
    buffer.push_back(0xFF);
    buffer.push_back(0xFF);

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer, offset);

    EXPECT_EQ(topology.getServers()[0].hashId, -1);
}

// Test parsing updates topology ID
TEST(TopologyInfoTest, TopologyIdUpdates) {
    ByteArray buffer1;
    Codec::writeVInt(buffer1, 1);
    Codec::writeVInt(buffer1, 0);  // No servers

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer1, offset);
    EXPECT_EQ(topology.getTopologyId(), 1);

    // Parse second update with higher ID
    ByteArray buffer2;
    Codec::writeVInt(buffer2, 10);
    Codec::writeVInt(buffer2, 0);

    offset = 0;
    topology.parseTopologyUpdate(buffer2, offset);
    EXPECT_EQ(topology.getTopologyId(), 10);
}

// ============================================================================
// Server Selection Tests (Round-Robin)
// ============================================================================

TEST(TopologyInfoTest, RoundRobinSelection) {
    TopologyInfo topology;
    topology.addServer(ServerInfo("server1", 11222, 1));
    topology.addServer(ServerInfo("server2", 11322, 2));
    topology.addServer(ServerInfo("server3", 11422, 3));

    // First selection
    const ServerInfo* server1 = topology.selectServer();
    ASSERT_NE(server1, nullptr);
    EXPECT_EQ(server1->host, "server1");

    // Second selection
    const ServerInfo* server2 = topology.selectServer();
    ASSERT_NE(server2, nullptr);
    EXPECT_EQ(server2->host, "server2");

    // Third selection
    const ServerInfo* server3 = topology.selectServer();
    ASSERT_NE(server3, nullptr);
    EXPECT_EQ(server3->host, "server3");

    // Fourth selection - wraps around
    const ServerInfo* server4 = topology.selectServer();
    ASSERT_NE(server4, nullptr);
    EXPECT_EQ(server4->host, "server1");
}

TEST(TopologyInfoTest, SelectServerWhenEmpty) {
    TopologyInfo topology;
    const ServerInfo* server = topology.selectServer();
    EXPECT_EQ(server, nullptr);
}

// ============================================================================
// Server Management Tests
// ============================================================================

TEST(TopologyInfoTest, AddServer) {
    TopologyInfo topology;

    topology.addServer(ServerInfo("server1", 11222, 1));
    EXPECT_EQ(topology.getServerCount(), 1);

    topology.addServer(ServerInfo("server2", 11322, 2));
    EXPECT_EQ(topology.getServerCount(), 2);
}

TEST(TopologyInfoTest, AddDuplicateServer) {
    TopologyInfo topology;

    topology.addServer(ServerInfo("server1", 11222, 1));
    EXPECT_EQ(topology.getServerCount(), 1);

    // Add same server again (should not duplicate)
    topology.addServer(ServerInfo("server1", 11222, 1));
    EXPECT_EQ(topology.getServerCount(), 1);
}

TEST(TopologyInfoTest, RemoveServer) {
    TopologyInfo topology;
    topology.addServer(ServerInfo("server1", 11222, 1));
    topology.addServer(ServerInfo("server2", 11322, 2));

    EXPECT_EQ(topology.getServerCount(), 2);

    topology.removeServer("server1", 11222);
    EXPECT_EQ(topology.getServerCount(), 1);
    EXPECT_EQ(topology.getServers()[0].host, "server2");
}

TEST(TopologyInfoTest, ClearServers) {
    TopologyInfo topology;
    topology.addServer(ServerInfo("server1", 11222, 1));
    topology.addServer(ServerInfo("server2", 11322, 2));

    EXPECT_TRUE(topology.hasServers());

    topology.clearServers();

    EXPECT_FALSE(topology.hasServers());
    EXPECT_EQ(topology.getServerCount(), 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(TopologyInfoTest, ParseInvalidBuffer_TooShort) {
    ByteArray buffer = {0x01};  // Only topology ID, missing server count

    TopologyInfo topology;
    size_t offset = 0;

    EXPECT_THROW({
        topology.parseTopologyUpdate(buffer, offset);
    }, std::runtime_error);
}

TEST(TopologyInfoTest, ParseInvalidBuffer_MissingPort) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);  // Topology ID
    Codec::writeVInt(buffer, 1);  // 1 server
    Codec::writeString(buffer, "server");
    // Missing port and hash ID

    TopologyInfo topology;
    size_t offset = 0;

    EXPECT_THROW({
        topology.parseTopologyUpdate(buffer, offset);
    }, std::runtime_error);
}

TEST(TopologyInfoTest, ParseInvalidBuffer_MissingHashId) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);
    Codec::writeVInt(buffer, 1);
    Codec::writeString(buffer, "server");
    buffer.push_back(0x2B); buffer.push_back(0xD6);  // port
    // Missing hash ID (4 bytes)

    TopologyInfo topology;
    size_t offset = 0;

    EXPECT_THROW({
        topology.parseTopologyUpdate(buffer, offset);
    }, std::runtime_error);
}

// ============================================================================
// IPv4 and IPv6 Hostname Tests
// ============================================================================

TEST(TopologyInfoTest, ParseIPv4Address) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);
    Codec::writeVInt(buffer, 1);
    Codec::writeString(buffer, "192.168.1.100");
    buffer.push_back(0x2B); buffer.push_back(0xD6);
    buffer.push_back(0x00); buffer.push_back(0x00);
    buffer.push_back(0x00); buffer.push_back(0x01);

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer, offset);

    EXPECT_EQ(topology.getServers()[0].host, "192.168.1.100");
}

TEST(TopologyInfoTest, ParseIPv6Address) {
    ByteArray buffer;
    Codec::writeVInt(buffer, 1);
    Codec::writeVInt(buffer, 1);
    Codec::writeString(buffer, "::1");  // IPv6 localhost
    buffer.push_back(0x2B); buffer.push_back(0xD6);
    buffer.push_back(0x00); buffer.push_back(0x00);
    buffer.push_back(0x00); buffer.push_back(0x01);

    TopologyInfo topology;
    size_t offset = 0;
    topology.parseTopologyUpdate(buffer, offset);

    EXPECT_EQ(topology.getServers()[0].host, "::1");
}
