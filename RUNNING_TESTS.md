# Running Integration Tests

## Prerequisites

- Docker installed and running
- Docker Compose v2
- Ports 11222-11523 available (used by Infinispan cluster)

## Quick Start

```bash
# 1. Build the project
cd build
cmake ..
make -j4

# 2. Clean up any leftover containers (if tests were interrupted before)
../scripts/cleanup_test_containers.sh

# 3. Run all tests
ctest --output-on-failure
```

## Available Tests

### Unit Tests
- `unit_tests` - Unit tests for codecs, header parsing, etc.

### Basic Integration Tests (Single Server)
- `ping_integration_tests` - PING operation
- `get_integration_tests` - GET operation
- `put_integration_tests` - PUT operation
- `remove_integration_tests` - REMOVE operation
- `concurrent_clients_tests` - Multiple concurrent clients

### Topology Tests (Multi-Server Cluster)
- `topology_integration_tests` - Dynamic topology changes (add/remove nodes)
- `failover_tests` - **Phase 1**: Connection failover on server failure
- `loadbalancing_tests` - **Phase 2**: Round-robin load balancing
- `connectionpool_tests` - **Phase 3**: Connection pool with reuse

## Running Specific Tests

### Run one test suite
```bash
./failover_tests
./loadbalancing_tests
./connectionpool_tests
```

### Run specific test case
```bash
./connectionpool_tests --gtest_filter=TopologyTest.ConnectionReuse
./loadbalancing_tests --gtest_filter=TopologyTest.RoundRobinDistribution
./failover_tests --gtest_filter=TopologyTest.FailoverOnServerDown
```

### Run with verbose output
```bash
./connectionpool_tests --gtest_filter=TopologyTest.ConnectionReuse -V
```

## Troubleshooting

### Error: "container name already in use"

This happens when a previous test was interrupted and left containers running.

**Solution:**
```bash
../scripts/cleanup_test_containers.sh
```

Or manually:
```bash
docker rm -f $(docker ps -aq --filter "name=ispn-node")
```

### Error: "Failed to start cluster"

**Possible causes:**
1. Docker not running → Start Docker
2. Leftover containers → Run `cleanup_test_containers.sh`
3. Ports already in use → Stop other services using ports 11222-11523
4. Scripts not executable → Run `chmod +x scripts/*.sh`

### Tests hang or timeout

The cluster might be slow to start. Check:
```bash
docker ps --filter "name=ispn-node"
docker logs ispn-node1
```

## Test Framework

### Multi-Server Tests

Tests using `TopologyTest` fixture (`topology_integration_tests`, `failover_tests`, `loadbalancing_tests`, `connectionpool_tests`) automatically:

1. Start a 3-node Infinispan cluster
2. Configure distributed caches
3. Run tests with dynamic topology changes
4. Clean up cluster after tests

The cluster management is handled by `MultiServerTestEnvironment` which:
- Starts cluster in `SetUp()`
- Provides `addNode()` / `removeNode()` for dynamic changes
- Stops cluster in `TearDown()`

### Observing Pool Behavior

To see connection pool messages, look for these log lines:

```
[POOL] Creating new connection to 172.18.0.4:11222  ← New connection
[POOL] Reusing existing connection to 172.18.0.4:11222  ← Reuse!
[POOL] Removing stale connection: localhost:11222  ← Cleanup
```

**Expected pattern for 30 operations across 3-node cluster:**
- First 3 operations: `Creating new connection` (one per server)
- Remaining 27 operations: `Reusing existing connection`
- **90% reduction in TCP connections!**

## Performance Comparison

### Before Connection Pool (Phases 1-2)
```
30 operations = 30 TCP connections created + 30 destroyed
```

### After Connection Pool (Phase 3)
```
30 operations = 3 TCP connections created + 27 reuses
```

## CI/CD Integration

Run tests in CI with proper cleanup:

```bash
#!/bin/bash
set -e

# Cleanup before tests
./scripts/cleanup_test_containers.sh

# Run tests
cd build
ctest --output-on-failure

# Cleanup after tests (even on failure)
trap '../scripts/cleanup_test_containers.sh' EXIT
```
