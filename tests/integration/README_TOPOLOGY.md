# Topology Integration Tests

Multi-server topology tests for Hot Rod client cluster awareness.

## Overview

These tests verify the Hot Rod client's ability to:
- Discover multiple servers in a cluster
- Detect topology changes (add/remove servers)
- Maintain data accessibility during topology changes
- Handle failover and rebalancing

**Based on Java tests**:
- `ReplTopologyChangeTest.java`
- `DistTopologyChangeTest.java`
- `RoundRobinBalancingIntegrationTest.java`
- `ConsistentHashV2IntegrationTest.java`

---

## Architecture

### Test Fixtures

#### `MultiServerTestEnvironment` (Global Environment)
- **Purpose**: Manages cluster lifecycle for all topology tests
- **Lifecycle**: SetUp once before all tests, TearDown once after all tests
- **Default**: Starts 3-node cluster
- **Features**:
  - Dynamic node add/remove
  - Cluster size verification
  - Server info access

#### `TopologyTestFixture` (Per-Test Fixture)
- **Purpose**: Per-test setup/teardown with helper methods
- **Features**:
  - Create clients connected to specific servers
  - Reset cluster to target size
  - Wait for topology updates
  - Clear test data

---

## Running the Tests

### Prerequisites

1. **Docker** installed and running
2. **Build the project**:
```bash
cd /home/rigazilla/git/cpp17-client
mkdir build && cd build
cmake ..
make
```

3. **Scripts are executable**:
```bash
chmod +x ../scripts/*.sh
```

### Run Topology Tests

```bash
# From build directory
cd build

# Run all topology tests
./topology_integration_tests

# Run with verbose output
./topology_integration_tests --gtest_verbose=1

# Run specific test
./topology_integration_tests --gtest_filter=TopologyTest.AddServer

# List all tests
./topology_integration_tests --gtest_list_tests
```

### Via CTest

```bash
# Run all integration tests (including topology)
ctest -R IntegrationTests --output-on-failure

# Run only topology tests
ctest -R TopologyIntegrationTests --output-on-failure

# Run with labels
ctest -L topology --output-on-failure
```

---

## Test Cases

### 1. ThreeNodeCluster
**Purpose**: Verify basic 3-node cluster formation

**Steps**:
1. Verify 3 nodes are running (default)
2. Check cluster size from server perspective
3. Create client and PING

**Expected**: 3-node cluster, PING succeeds

---

### 2. AddServer
**Purpose**: Add 4th server to 3-node cluster

**Steps**:
1. Start with 3 nodes
2. Put 10 keys
3. Add 4th node
4. Verify cluster size = 4
5. Wait for topology update in client
6. Verify all 10 keys are accessible
7. Put 10 more keys (total 20)
8. Verify all 20 keys accessible

**Expected**: Data accessible before and after adding node

**Java equivalent**: `ReplTopologyChangeTest.testAddNewServer()`

---

### 3. RemoveServer
**Purpose**: Remove node from 3-node cluster

**Steps**:
1. Start with 3 nodes
2. Put 15 keys
3. Remove node 3
4. Verify cluster size = 2
5. Wait for topology update
6. Verify all 15 keys still accessible

**Expected**: Data survives node removal (2 owners)

**Java equivalent**: `ReplTopologyChangeTest.testDropServer()`

---

### 4. ScaleUpAndDown
**Purpose**: Multiple topology changes (2→3→4→3→2)

**Steps**:
1. Reset to 2 nodes
2. Put 5 keys
3. Scale to 3 nodes, verify keys
4. Scale to 4 nodes, verify keys
5. Scale down to 3 nodes, verify keys
6. Scale down to 2 nodes, verify keys

**Expected**: Data survives all topology changes

**Java equivalent**: `DistTopologyChangeTest` (multiple changes)

---

### 5. MultipleClientsSeeTopologyChange
**Purpose**: Verify multiple clients detect topology changes

**Steps**:
1. Create 3 clients (one per node)
2. All clients PING successfully
3. Add 4th node
4. Wait for all clients to see topology update
5. All clients still work
6. Put via client1, get via client2 (cross-validation)

**Expected**: All clients see topology changes

---

### 6. DataSurvivesNodeRemoval
**Purpose**: Verify distributed cache with 2 owners survives node loss

**Steps**:
1. Start with 3 nodes
2. Put 30 keys (distributed across nodes)
3. Remove node 3
4. Verify all 30 keys still accessible

**Expected**: 100% key survival with numOwners=2

**Java equivalent**: `DistTopologyChangeTest` (data survival)

---

## Infrastructure

### Docker Compose Cluster

**File**: `test-configs/docker-compose-cluster.yml`

**Features**:
- 4-node Infinispan cluster
- Shared Docker network
- Fixed ports: 11222, 11322, 11422, 11522
- Health checks
- Distributed cache (2 owners, 256 segments)

### Cluster Management Scripts

**start_cluster.sh**
```bash
./scripts/start_cluster.sh [num_nodes]
# Starts N-node cluster (2-4)
# Exports: ISPN_CLUSTER_ID, ISPN_NODE{1-4}_HOST/PORT/CONTAINER
```

**stop_cluster.sh**
```bash
./scripts/stop_cluster.sh <cluster_id>
# Stops entire cluster
```

**add_cluster_node.sh**
```bash
./scripts/add_cluster_node.sh <cluster_id> <node_number>
# Dynamically adds node
```

**remove_cluster_node.sh**
```bash
./scripts/remove_cluster_node.sh <cluster_id> <node_number>
# Removes node from cluster
```

**wait_for_cluster_size.sh**
```bash
./scripts/wait_for_cluster_size.sh <cluster_id> <expected_size>
# Waits for cluster to reach size
```

---

## Debugging

### Manual Cluster Testing

```bash
# Start 3-node cluster
./scripts/start_cluster.sh 3 > /tmp/cluster-env.sh
source /tmp/cluster-env.sh

echo "Cluster ID: $ISPN_CLUSTER_ID"
echo "Node 1: $ISPN_NODE1_HOST:$ISPN_NODE1_PORT"
echo "Node 2: $ISPN_NODE2_HOST:$ISPN_NODE2_PORT"
echo "Node 3: $ISPN_NODE3_HOST:$ISPN_NODE3_PORT"

# Check cluster size
curl -s --digest -u admin:password \
  "http://localhost:11222/rest/v2/cluster?action=distribution" | \
  grep -o "node_name" | wc -l

# Add node 4
./scripts/add_cluster_node.sh $ISPN_CLUSTER_ID 4

# Remove node 3
./scripts/remove_cluster_node.sh $ISPN_CLUSTER_ID 3

# Stop cluster
./scripts/stop_cluster.sh $ISPN_CLUSTER_ID
```

### View Container Logs

```bash
# All nodes
docker-compose -f test-configs/docker-compose-cluster.yml -p $ISPN_CLUSTER_ID logs -f

# Specific node
docker logs ${ISPN_CLUSTER_ID}_ispn-node1_1 -f
```

### Check Cluster Health

```bash
# Via REST API
curl -s --digest -u admin:password \
  "http://localhost:11222/rest/v2/cluster?action=health"
```

---

## Common Issues

### Issue: Script permission denied
**Solution**:
```bash
chmod +x scripts/*.sh
```

### Issue: Docker not running
**Error**: `Failed to start cluster. Check Docker is running.`
**Solution**: Start Docker daemon

### Issue: Port already in use
**Solution**: Stop existing containers
```bash
docker ps
docker stop <container_id>
```

### Issue: Cluster doesn't form
**Check**:
```bash
docker-compose -f test-configs/docker-compose-cluster.yml -p <cluster_id> logs
```

### Issue: Test timeout
**Increase timeout** in `wait_for_cluster_size.sh` (default: 60s)

---

## Next Steps

### Phase 3: Utilities (TODO)
- [ ] REST API client for key distribution verification
- [ ] Key generator for hash-aware routing tests
- [ ] Server statistics helpers

### Phase 4: Advanced Tests (TODO)
- [ ] RoundRobinBalancingTest - Verify load distribution
- [ ] HashAwareRoutingTest - Verify primary owner routing
- [ ] FailoverTest - Verify failover to backup owners

---

## Files

### Test Code
- `tests/integration/MultiServerTestEnvironment.h` - Global environment
- `tests/integration/TopologyTestFixture.h` - Per-test fixture
- `tests/integration/TopologyChangeTest.cpp` - Test cases

### Infrastructure
- `test-configs/docker-compose-cluster.yml` - Multi-node cluster
- `test-configs/infinispan-cluster.xml` - Cluster configuration
- `scripts/start_cluster.sh` - Start cluster
- `scripts/stop_cluster.sh` - Stop cluster
- `scripts/add_cluster_node.sh` - Add node
- `scripts/remove_cluster_node.sh` - Remove node
- `scripts/wait_for_cluster_size.sh` - Wait for size

### Documentation
- `docs/topology-test-architecture.md` - Architecture design
- `docs/TOPOLOGY_TESTING_SETUP.md` - Setup guide
- `tests/integration/README_TOPOLOGY.md` - This file

---

**Status**: Phase 2 Complete ✅  
**Next**: Phase 3 - REST API utilities for routing verification
