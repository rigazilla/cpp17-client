# Infinispan Hot Rod Client - C++17

A cross-platform C++17 implementation of the Infinispan Hot Rod protocol client.

🎉 **Hash-Aware Routing Complete!** - Smart client with automatic failover.

## Quick Start

See the complete working example in [`examples/quickstart/`](examples/quickstart/):

```bash
cd examples/quickstart
./start-server.sh        # Start Infinispan 16.2
mkdir build && cd build
cmake .. && make
./quickstart             # Run the example
```

Example output:
```
=== Hot Rod C++ Client Quickstart ===
Connecting to Infinispan server...
Connected successfully!

--- PUT Operations ---
PUT: greeting = Hello, Infinispan!
PUT: language = C++17
PUT: version = 1.0.0

--- GET Operations ---
GET: greeting = Hello, Infinispan!
GET: language = C++17
GET: version = 1.0.0

--- REMOVE Operations ---
REMOVE: language deleted successfully

=== Quickstart Complete ===
```

The quickstart demonstrates:
- Connecting to Infinispan
- PUT operations (storing key-value pairs)
- GET operations (reading values)
- REMOVE operations (deleting entries)
- Clean disconnect

See [`examples/quickstart/README.md`](examples/quickstart/README.md) for full documentation.

## Building

### Requirements

**Build Requirements**:
- **C++17** compatible compiler (GCC 7+, Clang 6+, MSVC 19.20+)
- **CMake** 3.15+
- **OpenSSL** 1.1.1+ or 3.x
- **Google Test** (for testing)

**Runtime Requirements**:
- Infinispan Server 16.0+ (tested with 16.2)
- Docker (for integration tests and quickstart example)

### Linux (Fedora/RHEL)

```bash
# Install dependencies
sudo dnf install cmake gcc-c++ openssl-devel gtest-devel

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt install cmake g++ libssl-dev libgtest-dev

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Windows (PowerShell)

```powershell
# Install dependencies via vcpkg
vcpkg install openssl gtest

# Build
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release

# Run tests
ctest -C Release --output-on-failure
```

## Testing

### Unit Tests (155 tests)
```bash
cd build
./unit_tests

# Or via CTest
ctest -R UnitTests --output-on-failure
```

### Integration Tests (38 tests)
Requires Docker to run Infinispan server:
```bash
cd build

# Run all integration tests (starts server automatically)
./ping_integration_tests
./get_integration_tests  
./put_integration_tests
./remove_integration_tests
./topology_integration_tests         # Multi-node cluster tests
./hash_aware_routing_integration_tests  # Smart routing tests

# Or run all via CTest
ctest --output-on-failure
```

**Note**: Integration tests use Docker Compose to manage multi-node Infinispan clusters (up to 4 nodes). Tests verify failover, rebalancing, and hash-aware routing.

## Status

**Current Progress**: Steps 0-12 COMPLETE ✅ - Full CRUD + Hash-Aware Routing! See [PROGRESS.md](PROGRESS.md)

**Test Results**:
- Unit Tests: **155/155 passing** ✅
- Integration Tests: **38/38 passing** ✅ (against live Infinispan 16.2 server)

## Features

### Implemented ✅
- ✅ **Cross-platform support** (Linux + Windows)
- ✅ **Wire format primitives** (vInt, vLong, strings, byte arrays)
- ✅ **Protocol 4.0 headers** (complete spec with all conditional fields)
- ✅ **Client Intelligence 0x03** (HASH_DISTRIBUTION_AWARE)
- ✅ **Hash-aware routing** (keys route directly to primary owner)
- ✅ **Automatic failover** (primary → backup owners → any server)
- ✅ **Topology awareness** (cluster rebalancing, updates)
- ✅ **Consistent hashing** (MurmurHash3 x64_32, segment-based routing)
- ✅ **Connection pooling** (one connection per server)
- ✅ **Multiplexed connections** (concurrent async operations on single connection per server)
- ✅ **Async operations** (all operations return `std::future` for non-blocking execution)
- ✅ **PING operation** (server connectivity check)
- ✅ **GET operation** (read from cache with failover)
- ✅ **PUT operation** (write to cache with lifespan/maxIdle)
- ✅ **REMOVE operation** (delete from cache)
- ✅ **Integration test framework** (GoogleTest + Docker + multi-node clusters)

### Full CRUD with Smart Routing and Async Operations
```cpp
// Connect with hash-aware routing
RemoteCache cache("localhost", 11222, "my-cache");
cache.setClientIntelligence(ClientIntelligence::HASH_DISTRIBUTION_AWARE);
cache.connect();

// All operations are async (return std::future) and can run concurrently
// Multiple operations share a single multiplexed connection per server

// CREATE/UPDATE - routes to primary owner
auto putFuture = cache.put(key, value, lifespan, maxIdle);

// READ - automatic failover to backup if primary fails
auto getFuture = cache.get(key);

// DELETE - with automatic failover
auto removeFuture = cache.remove(key);

// PING
auto pingFuture = cache.ping();

// Wait for results when needed
auto result = getFuture.get();  // blocks until complete
if (result.has_value()) {
    ByteArray value = result.value();
}

// Or fire-and-forget for max throughput
for (int i = 0; i < 1000; i++) {
    cache.put(keys[i], values[i]);  // returns immediately, executes concurrently
}
```

### Future
- GET_WITH_METADATA (Step 10)
- Version-based operations (REPLACE_IF_UNMODIFIED)
- SCRAM-SHA-256 authentication (Step 11)
- TLS/SSL support
- Bulk operations

## Project Structure

```
cpp17-client/
├── CMakeLists.txt               # Build configuration
├── README.md                    # This file
├── PROGRESS.md                  # Implementation progress
├── LICENSE                      # Apache 2.0
├── include/hotrod/              # Public API headers
│   ├── Types.h                  # Type definitions
│   ├── Codec.h                  # Wire format primitives
│   ├── HeaderCodec.h            # Protocol 4.0 headers
│   ├── Connection.h             # TCP connection
│   ├── RemoteCache.h            # High-level API (with hash-aware routing)
│   ├── TopologyInfo.h           # Cluster tracking
│   ├── ConsistentHash.h         # Smart routing
│   └── MurmurHash3.h            # Hash function
├── src/                         # Implementation
│   ├── codec/                   # Encoding/decoding
│   ├── transport/               # Network I/O
│   ├── topology/                # Cluster tracking
│   ├── hash/                    # MurmurHash3 + consistent hashing
│   └── operations/              # Hot Rod operations (with failover)
├── tests/                       # Test suite
│   ├── unit/                    # 155 unit tests
│   └── integration/             # 38 integration tests (multi-node clusters)
├── examples/                    # Usage examples
│   └── quickstart/              # Simple GET/PUT/REMOVE example
├── scripts/                     # Test infrastructure
│   ├── start_cluster.sh         # Start multi-node cluster
│   ├── add_cluster_node.sh      # Add node dynamically
│   └── remove_cluster_node.sh   # Remove node for failover tests
└── test-configs/                # Server configurations
    ├── docker-compose-cluster.yml  # Multi-node setup
    └── infinispan-cluster.xml      # No-auth config
```

## Development

This implementation follows the **hotrod-foundry** incremental approach:

1. **Test-driven development** - Test vectors → Unit tests → Implementation
2. **Java reference** - Study Java implementation for each step (MANDATORY)
3. **Byte-level validation** - Compare with test vectors
4. **Cross-platform** - Linux + Windows support required
5. **Infrastructure-first** - Steps 0-5 (foundation), then operations

### Roadmap

See [hotrod-foundry/ROADMAP.md](../hotrod-foundry/ROADMAP.md) for the complete step-by-step plan.

### Java Reference

Located at: `/home/rigazilla/git/infinispan/client/hotrod-client/`

## References

- **Infinispan Project**: https://infinispan.org/
- **Hot Rod Foundry**: ../hotrod-foundry/
- **Java Client Source**: /home/rigazilla/git/infinispan/
- **Protocol Specification**: https://infinispan.org/docs/stable/titles/hotrod_protocol/

## License

Apache 2.0 (matching Infinispan project)

## Contributing

Following the hotrod-foundry porting guidelines:
- One step at a time (complete step N before step N+1)
- Update PROGRESS.md after each step
- Validate against test vectors
- Ensure cross-platform compatibility

## Implementation Milestones

- ✅ **v0.1.0** (2026-06-11): Foundation complete (Steps 0-5)
- ✅ **v0.2.0** (2026-06-12): PING operation complete (Step 6)
- ✅ **v0.3.0** (2026-06-12): GET operation complete (Step 7)
- ✅ **v0.4.0** (2026-06-12): PUT operation complete (Step 8)
- ✅ **v0.5.0** (2026-06-12): REMOVE operation complete (Step 9) - **Full CRUD!**
- ✅ **v0.6.0** (2026-06-17): Topology awareness (Step 12)
- ✅ **v0.7.0** (2026-06-17): Hash-aware routing with failover - **Smart Client!**
- 🎯 **v0.8.0** (upcoming): Metadata operations (Step 10)

---

**Maintained by**: rigazilla  
**Status**: Active development  
**Current Step**: Hash-aware routing complete! Smart client with automatic failover. ✅
