# Multi-Server Topology Test Architecture

## Overview

Architecture for testing Hot Rod client topology awareness with multiple Infinispan servers, matching Java test suite patterns.

**Target**: Support 2-4 server clusters for topology change tests

**Based on**:
- Java: `ReplTopologyChangeTest.java` (2-3 servers)
- Java: `RoundRobinBalancingIntegrationTest.java` (3-4 servers)
- Java: `ConsistentHashV2IntegrationTest.java` (4 servers)
- Go: `topology_change_test.go` (3 servers with Docker network)

---

## Architecture Components

### 1. Docker Compose Cluster (Infrastructure)

**File**: `test-configs/docker-compose-cluster.yml`

**Configuration**:
```yaml
services:
  ispn-node1:
    image: quay.io/infinispan/server:16.0
    ports: ["11222:11222"]
    networks: [ispn-cluster]
    
  ispn-node2:
    image: quay.io/infinispan/server:16.0
    ports: ["11322:11222"]
    networks: [ispn-cluster]
    
  ispn-node3:
    image: quay.io/infinispan/server:16.0
    ports: ["11422:11222"]
    networks: [ispn-cluster]
    
  ispn-node4:
    image: quay.io/infinispan/server:16.0
    ports: ["11522:11222"]
    networks: [ispn-cluster]
```

**Features**:
- Shared Docker network for clustering
- Fixed port mappings for predictable access
- Anonymous mode (no authentication) for simplicity
- All nodes can discover each other automatically

---

### 2. Cluster Management Scripts (Bash)

**Purpose**: Start/stop cluster and individual nodes

#### Script: `start_cluster.sh`
```bash
# Start N nodes (default: 3)
# Usage: ./start_cluster.sh [num_nodes]
# Output: exports ISPN_CLUSTER_ID, ISPN_NODE1_HOST, ISPN_NODE1_PORT, etc.
```

#### Script: `stop_cluster.sh`
```bash
# Stop entire cluster
# Usage: ./stop_cluster.sh <cluster_id>
```

#### Script: `add_cluster_node.sh`
```bash
# Dynamically add a node to running cluster
# Usage: ./add_cluster_node.sh <cluster_id> <node_number>
```

#### Script: `remove_cluster_node.sh`
```bash
# Stop specific node in cluster
# Usage: ./remove_cluster_node.sh <cluster_id> <node_number>
```

#### Script: `wait_for_cluster_size.sh`
```bash
# Wait until cluster reaches expected size
# Usage: ./wait_for_cluster_size.sh <cluster_id> <expected_size>
```

---

### 3. C++ Test Fixtures (GoogleTest)

#### Fixture: `MultiServerTestEnvironment` (Global Environment)

**Purpose**: Manage cluster lifecycle for all topology tests

```cpp
class MultiServerTestEnvironment : public ::testing::Environment {
public:
    static std::string clusterID;
    static std::vector<ServerInfo> servers;  // host, port, containerID
    static int numServers;
    
    void SetUp() override {
        // Start cluster with default 3 nodes
        startCluster(3);
    }
    
    void TearDown() override {
        // Stop entire cluster
        stopCluster();
    }
    
    static void startCluster(int nodeCount);
    static void stopCluster();
    static void addNode();
    static void removeNode(int nodeIndex);
    static void waitForClusterSize(int expectedSize);
};
```

#### Fixture: `TopologyTestFixture` (Per-Test Fixture)

**Purpose**: Setup/teardown for individual topology tests

```cpp
class TopologyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset cluster to 3 nodes
        MultiServerTestEnvironment::resetCluster(3);
    }
    
    void TearDown() override {
        // Cleanup test data
    }
    
    // Helper methods
    RemoteCache* createClient(int serverIndex);
    std::vector<std::string> getKeysOnServer(int serverIndex);
    ServerStats getServerStats(int serverIndex);
};
```

---

### 4. REST API Helper Utilities

**Purpose**: Verify server-side state via REST API

#### Class: `RestAPIClient`

```cpp
class RestAPIClient {
public:
    RestAPIClient(const std::string& host, int port);
    
    // Key distribution
    std::vector<std::string> getKeys(const std::string& cacheName);
    int getKeyCount(const std::string& cacheName);
    
    // Server statistics
    ServerStats getStats(const std::string& cacheName);
    
    // Cluster info
    int getClusterSize();
    std::vector<std::string> getClusterMembers();
    
    // Cache operations (for cross-validation)
    void putViaREST(const std::string& cacheName, 
                    const std::string& key, 
                    const std::string& value);
    std::string getViaREST(const std::string& cacheName, 
                          const std::string& key);
};
```

#### Struct: `ServerStats`

```cpp
struct ServerStats {
    int hits;
    int misses;
    int stores;
    int retrievals;
    int removes;
    int currentSize;
};
```

---

### 5. Key Generator Utilities

**Purpose**: Generate keys that hash to specific segments

#### Class: `KeyGenerator`

```cpp
class KeyGenerator {
public:
    // Generate key that hashes to target segment
    ByteArray generateKeyForSegment(int targetSegment, 
                                    int numSegments = 256,
                                    int seed = 9001);
    
    // Generate N keys for same segment
    std::vector<ByteArray> generateKeysForSegment(int targetSegment,
                                                   int count,
                                                   int numSegments = 256);
    
    // Get segment for a key
    int getSegment(const ByteArray& key, 
                   int numSegments = 256,
                   int seed = 9001);
};
```

---

## Test Structure

### Directory Layout

```
cpp17-client/
├── test-configs/
│   ├── docker-compose-cluster.yml        # Multi-node cluster config
│   └── infinispan-cluster-noauth.xml     # Cluster XML config
├── scripts/
│   ├── start_cluster.sh                  # Start N-node cluster
│   ├── stop_cluster.sh                   # Stop cluster
│   ├── add_cluster_node.sh               # Add node dynamically
│   ├── remove_cluster_node.sh            # Remove node
│   └── wait_for_cluster_size.sh          # Wait for cluster ready
├── tests/integration/
│   ├── MultiServerTestEnvironment.h      # Global cluster env
│   ├── TopologyTestFixture.h             # Per-test fixture
│   ├── RestAPIClient.h/cpp               # REST helpers
│   ├── KeyGenerator.h/cpp                # Key generator
│   └── topology/
│       ├── TopologyChangeTest.cpp        # Add/remove servers
│       ├── RoundRobinBalancingTest.cpp   # Load distribution
│       └── HashAwareRoutingTest.cpp      # Primary owner routing
```

---

## Server Configuration

### Cluster XML Configuration

**File**: `test-configs/infinispan-cluster-noauth.xml`

**Key Settings**:
```xml
<cache-container>
  <transport cluster="ISPN" />  <!-- Enable clustering -->
  <distributed-cache name="default">
    <partition-handling when-split="ALLOW_READ_WRITES" />
    <state-transfer enabled="true" />
    <groups enabled="true"/>
  </distributed-cache>
</cache-container>
```

**Requirements**:
- Anonymous access (no authentication)
- Distributed cache mode
- 2 owners (numOwners=2)
- 256 segments (default)
- State transfer enabled

---

## Implementation Phases

### Phase 1: Infrastructure Setup ✅
1. ✅ Create Docker Compose configuration
2. ✅ Implement cluster management scripts
3. ✅ Test cluster formation (3 nodes)
4. ✅ Verify cluster discovery via REST API

### Phase 2: C++ Test Fixtures ✅
1. ✅ Implement MultiServerTestEnvironment
2. ✅ Implement TopologyTestFixture
3. ✅ Test cluster lifecycle in GoogleTest

### Phase 3: Utilities ✅
1. ✅ Implement RestAPIClient
2. ✅ Implement KeyGenerator
3. ✅ Test utilities with running cluster

### Phase 4: Topology Tests 🎯
1. 🎯 Implement TopologyChangeTest
2. 🎯 Implement RoundRobinBalancingTest
3. 🎯 Implement HashAwareRoutingTest

---

## Test Scenarios

### Scenario 1: Basic Topology Change (2→3→2 nodes)
```cpp
TEST_F(TopologyTest, AddAndRemoveServer) {
    // Start: 2 nodes
    auto cache = createClient(0);
    EXPECT_EQ(2, getTopologySize(cache));
    
    // Add node 3
    addNode();
    waitForTopologyUpdate(cache, 3);
    EXPECT_EQ(3, getTopologySize(cache));
    
    // Remove node 3
    removeNode(2);
    waitForTopologyUpdate(cache, 2);
    EXPECT_EQ(2, getTopologySize(cache));
}
```

### Scenario 2: Round-Robin Distribution
```cpp
TEST_F(TopologyTest, RoundRobinLoadBalancing) {
    // 3 servers, TOPOLOGY_AWARE (intelligence 0x02)
    auto cache = createClient(0);
    
    // 9 PUTs
    for (int i = 0; i < 9; i++) {
        cache->put(key(i), value(i));
    }
    
    // Verify: 3 keys per server (round-robin)
    EXPECT_EQ(3, getKeysOnServer(0).size());
    EXPECT_EQ(3, getKeysOnServer(1).size());
    EXPECT_EQ(3, getKeysOnServer(2).size());
}
```

### Scenario 3: Hash-Aware Primary Owner
```cpp
TEST_F(TopologyTest, SmartRoutingToPrimaryOwner) {
    // 3 servers, HASH_DISTRIBUTION_AWARE (intelligence 0x03)
    auto cache = createClient(0);
    
    // Generate 100 keys for segment 0
    auto keys = keyGen.generateKeysForSegment(0, 100);
    
    for (const auto& key : keys) {
        cache->put(key, "value");
    }
    
    // Verify: Server owning segment 0 has most keys
    auto stats0 = getServerStats(0);
    auto stats1 = getServerStats(1);
    auto stats2 = getServerStats(2);
    
    // Expect >95% hit rate on primary owner
    EXPECT_GT(stats0.stores, 95);
    EXPECT_LT(stats1.stores + stats2.stores, 5);
}
```

---

## Verification Methods

### Method 1: Topology Size (Client-Side)
```cpp
int getTopologySize(RemoteCache* cache) {
    // Parse topology from client's internal state
    // (requires exposing topology info in RemoteCache API)
}
```

### Method 2: Key Distribution (Server-Side via REST)
```cpp
std::vector<std::string> getKeysOnServer(int serverIndex) {
    RestAPIClient rest(servers[serverIndex].host, servers[serverIndex].port);
    return rest.getKeys("default");
}
```

### Method 3: Server Statistics (Server-Side via REST)
```cpp
ServerStats getServerStats(int serverIndex) {
    RestAPIClient rest(servers[serverIndex].host, servers[serverIndex].port);
    return rest.getStats("default");
}
```

---

## Comparison to Existing Infrastructure

### Current (Single Server)
- ✅ `InfinispanTestEnvironment` - single server
- ✅ Anonymous mode XML config
- ✅ Start/stop scripts

### New (Multi-Server)
- ✅ `MultiServerTestEnvironment` - cluster
- ✅ Docker Compose for clustering
- ✅ Dynamic add/remove nodes
- ✅ REST API verification
- ✅ Key generator utilities

---

## Next Steps

1. ✅ Implement Docker Compose configuration
2. ✅ Implement cluster management scripts
3. ✅ Implement MultiServerTestEnvironment
4. ✅ Implement RestAPIClient
5. ✅ Implement KeyGenerator
6. 🎯 Write first topology test (TopologyChangeTest)
7. 🎯 Write round-robin test
8. 🎯 Write hash-aware routing test

---

**Status**: Architecture designed, ready for implementation
**Target**: Match Java test suite patterns (2-4 servers)
**Coverage Goal**: Implement missing topology tests from Java
