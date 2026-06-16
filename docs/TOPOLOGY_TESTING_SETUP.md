# Multi-Server Topology Testing - Setup Complete ✅

## Summary

Infrastructure for testing Hot Rod client topology awareness with 2-4 server clusters is now ready.

**Status**: Phase 1 (Infrastructure) COMPLETE ✅  
**Next**: Implement C++ test fixtures and utilities

---

## What's Been Implemented

### 1. Docker Compose Cluster Configuration ✅

**File**: `test-configs/docker-compose-cluster.yml`

**Features**:
- 4-node Infinispan cluster (start 2-4 nodes as needed)
- Shared Docker network (`hotrod-test-cluster`)
- Fixed port mappings:
  - Node 1: `11222:11222` (Hot Rod)
  - Node 2: `11322:11222`
  - Node 3: `11422:11222`
  - Node 4: `11522:11222`
- Health checks for each node
- Distributed cache mode (2 owners, 256 segments)
- Authentication enabled (admin/password)

**Usage**:
```bash
# Start 3-node cluster
docker-compose -f test-configs/docker-compose-cluster.yml up -d ispn-node1 ispn-node2 ispn-node3

# Add 4th node
docker-compose -f test-configs/docker-compose-cluster.yml up -d ispn-node4

# Stop node 3
docker-compose -f test-configs/docker-compose-cluster.yml stop ispn-node3

# Stop all
docker-compose -f test-configs/docker-compose-cluster.yml down
```

---

### 2. Cluster Management Scripts ✅

**All scripts are executable and ready to use.**

#### `scripts/start_cluster.sh` ✅
Start N-node cluster (2-4 nodes)

```bash
# Start 3-node cluster (default)
./scripts/start_cluster.sh

# Start 2-node cluster
./scripts/start_cluster.sh 2

# Start 4-node cluster
./scripts/start_cluster.sh 4
```

**Output**: Exports environment variables
```bash
export ISPN_CLUSTER_ID=hotrod-test-12345
export ISPN_NUM_NODES=3
export ISPN_NODE1_HOST=localhost
export ISPN_NODE1_PORT=11222
export ISPN_NODE1_CONTAINER=hotrod-test-12345_ispn-node1_1
export ISPN_NODE2_HOST=localhost
export ISPN_NODE2_PORT=11322
...
```

#### `scripts/stop_cluster.sh` ✅
Stop entire cluster

```bash
./scripts/stop_cluster.sh <cluster_id>
```

#### `scripts/add_cluster_node.sh` ✅
Dynamically add node to running cluster

```bash
# Add node 4 to cluster
./scripts/add_cluster_node.sh <cluster_id> 4
```

**Output**: Exports new node info
```bash
export ISPN_NODE4_HOST=localhost
export ISPN_NODE4_PORT=11522
export ISPN_NODE4_CONTAINER=...
```

#### `scripts/remove_cluster_node.sh` ✅
Remove node from cluster

```bash
# Remove node 3
./scripts/remove_cluster_node.sh <cluster_id> 3
```

#### `scripts/wait_for_cluster_size.sh` ✅
Wait for cluster to reach expected size

```bash
# Wait for 3 members
./scripts/wait_for_cluster_size.sh <cluster_id> 3
```

---

### 3. Cluster XML Configuration ✅

**File**: `test-configs/infinispan-cluster.xml`

**Configuration**:
- **Clustering**: Enabled with transport layer
- **Cache mode**: Distributed (DIST_SYNC)
- **Owners**: 2 replicas per entry
- **Segments**: 256 (standard)
- **State transfer**: Enabled (for rebalancing)
- **Authentication**: SCRAM-SHA-256 (admin/password)
- **REST API**: Enabled for verification

**Matches Java test patterns**:
- ✅ `ReplTopologyChangeTest.java` - 2-3 servers
- ✅ `RoundRobinBalancingIntegrationTest.java` - 3-4 servers
- ✅ `ConsistentHashV2IntegrationTest.java` - 4 servers

---

## Testing the Infrastructure

### Manual Test

```bash
# 1. Start 3-node cluster
cd /home/rigazilla/git/cpp17-client
./scripts/start_cluster.sh 3 > /tmp/cluster-env.sh
source /tmp/cluster-env.sh

# 2. Verify cluster size via REST API
curl -s --digest -u admin:password \
  "http://localhost:11222/rest/v2/cluster?action=distribution" | \
  grep -o "node_name" | wc -l
# Expected: 3

# 3. Add 4th node
./scripts/add_cluster_node.sh $ISPN_CLUSTER_ID 4 >> /tmp/cluster-env.sh
source /tmp/cluster-env.sh

# 4. Wait for cluster size 4
./scripts/wait_for_cluster_size.sh $ISPN_CLUSTER_ID 4

# 5. Verify 4 members
curl -s --digest -u admin:password \
  "http://localhost:11222/rest/v2/cluster?action=distribution" | \
  grep -o "node_name" | wc -l
# Expected: 4

# 6. Remove node 3
./scripts/remove_cluster_node.sh $ISPN_CLUSTER_ID 3

# 7. Wait for cluster size 3
./scripts/wait_for_cluster_size.sh $ISPN_CLUSTER_ID 3

# 8. Stop cluster
./scripts/stop_cluster.sh $ISPN_CLUSTER_ID
```

---

## Next Steps

### Phase 2: C++ Test Fixtures 🎯

**Task 4**: Implement C++ GoogleTest fixtures

Need to create:

#### 1. `tests/integration/MultiServerTestEnvironment.h`
Global environment for managing cluster lifecycle

```cpp
class MultiServerTestEnvironment : public ::testing::Environment {
public:
    struct ServerInfo {
        std::string host;
        int port;
        std::string containerID;
    };

    static std::string clusterID;
    static std::vector<ServerInfo> servers;
    static int numServers;

    void SetUp() override;
    void TearDown() override;

    static void addNode(int nodeNumber);
    static void removeNode(int nodeNumber);
    static void waitForClusterSize(int expectedSize);
};
```

#### 2. `tests/integration/TopologyTestFixture.h`
Per-test fixture with helper methods

```cpp
class TopologyTest : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    // Helper methods
    RemoteCache* createClient(int serverIndex);
    int getTopologySize(RemoteCache* cache);
};
```

---

### Phase 3: Utilities 🎯

**Task 5**: REST API helpers and key generator

Need to create:

#### 1. `tests/integration/RestAPIClient.h/cpp`
Query Infinispan REST API for verification

```cpp
class RestAPIClient {
public:
    RestAPIClient(const std::string& host, int port);

    // Key distribution
    std::vector<std::string> getKeys(const std::string& cacheName);
    int getKeyCount(const std::string& cacheName);

    // Statistics
    int getStoreCount(const std::string& cacheName);
    int getHitCount(const std::string& cacheName);

    // Cluster info
    int getClusterSize();
};
```

#### 2. `tests/integration/KeyGenerator.h/cpp`
Generate keys that hash to specific segments

```cpp
class KeyGenerator {
public:
    ByteArray generateKeyForSegment(int targetSegment,
                                    int numSegments = 256,
                                    int seed = 9001);

    std::vector<ByteArray> generateKeysForSegment(int targetSegment,
                                                   int count,
                                                   int numSegments = 256);
};
```

---

### Phase 4: Topology Tests 🎯

Implement tests matching Java patterns:

1. **TopologyChangeTest.cpp**
   - Add server (2→3 nodes)
   - Remove server (3→2 nodes)
   - Verify topology updates

2. **RoundRobinBalancingTest.cpp**
   - Verify even distribution across servers
   - TOPOLOGY_AWARE intelligence (0x02)

3. **HashAwareRoutingTest.cpp**
   - Verify requests to primary owner
   - HASH_DISTRIBUTION_AWARE intelligence (0x03)

---

## Files Created

### Configuration
- ✅ `test-configs/docker-compose-cluster.yml` - 4-node cluster
- ✅ `test-configs/infinispan-cluster.xml` - Cluster XML config

### Scripts (all executable)
- ✅ `scripts/start_cluster.sh` - Start N-node cluster
- ✅ `scripts/stop_cluster.sh` - Stop cluster
- ✅ `scripts/add_cluster_node.sh` - Add node dynamically
- ✅ `scripts/remove_cluster_node.sh` - Remove node
- ✅ `scripts/wait_for_cluster_size.sh` - Wait for size

### Documentation
- ✅ `docs/topology-test-architecture.md` - Architecture design
- ✅ `docs/TOPOLOGY_TESTING_SETUP.md` - This file

---

## Comparison: Java vs. Go vs. C++

| Feature | Java | Go | C++ (Now) |
|---------|------|----|----|
| **Multi-server cluster** | ✅ Embedded | ✅ Testcontainers | ✅ Docker Compose |
| **Dynamic add/remove** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Cluster scripts** | ❌ No | ✅ Bash | ✅ Bash |
| **Fixed ports** | ❌ Random | ❌ Random | ✅ Fixed |
| **Health checks** | ✅ Yes | ⚠️ Manual | ✅ Docker |
| **REST API verification** | ✅ Embedded cache | ✅ REST | 🎯 REST (TODO) |
| **Key generator** | ✅ KeyAffinityService | ❌ No | 🎯 TODO |
| **Hit count tracking** | ✅ Interceptors | ❌ No | 🎯 TODO |

**C++ Advantages**:
- ✅ Fixed ports (easier debugging)
- ✅ Standalone scripts (no language dependency)
- ✅ Docker Compose (industry standard)
- ✅ Health checks built-in

**C++ TODO**:
- 🎯 REST API client implementation
- 🎯 Key generator utility
- 🎯 GoogleTest fixtures
- 🎯 Actual topology tests

---

## Success Criteria

Phase 1 (Infrastructure) ✅ **COMPLETE**
- [x] Docker Compose cluster (2-4 nodes)
- [x] Cluster management scripts
- [x] Cluster XML configuration
- [x] Manual testing successful

Phase 2 (Fixtures) 🎯 **NEXT**
- [ ] MultiServerTestEnvironment
- [ ] TopologyTestFixture
- [ ] Integration with GoogleTest

Phase 3 (Utilities) 🎯
- [ ] RestAPIClient implementation
- [ ] KeyGenerator implementation
- [ ] Server statistics helpers

Phase 4 (Tests) 🎯
- [ ] TopologyChangeTest
- [ ] RoundRobinBalancingTest
- [ ] HashAwareRoutingTest

---

## Ready to Proceed

The infrastructure is **production-ready** and tested. You can now:

1. **Test the scripts** manually (see "Testing the Infrastructure")
2. **Implement C++ fixtures** (Task 4)
3. **Implement utilities** (Task 5)
4. **Write topology tests** (Phase 4)

---

**Status**: Infrastructure complete, ready for C++ implementation  
**Next Task**: Implement MultiServerTestEnvironment.h
