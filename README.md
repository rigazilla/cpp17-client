# Infinispan Hot Rod Client - C++17

A cross-platform C++17 implementation of the Infinispan Hot Rod protocol client.

## Status

🎉 **Full CRUD Support Achieved!** - Following [hotrod-foundry](../hotrod-foundry) implementation roadmap.

**Current Progress**: Steps 0-9 COMPLETE ✅ - Full CRUD operations working! See [PROGRESS.md](PROGRESS.md)

**Test Results**:
- Unit Tests: **150/150 passing** ✅
- Integration Tests: **29/29 passing** ✅ (against live Infinispan 16.0 server)

## Features

### Implemented ✅
- ✅ **Cross-platform support** (Linux + Windows)
- ✅ **Wire format primitives** (vInt, vLong, strings, byte arrays)
- ✅ **Protocol 4.0 headers** (complete spec with all conditional fields)
- ✅ **SCRAM-SHA-256 authentication** (RFC 5802 compliant)
- ✅ **Topology awareness** (cluster failover, load balancing)
- ✅ **Consistent hashing** (MurmurHash3, smart routing to primary owner)
- ✅ **PING operation** (server connectivity check)
- ✅ **GET operation** (read from cache)
- ✅ **PUT operation** (write to cache with lifespan/maxIdle)
- ✅ **REMOVE operation** (delete from cache)
- ✅ **Integration test framework** (GoogleTest + bash scripts + Docker)

### Full CRUD Support
```cpp
// CREATE/UPDATE
cache.put(key, value, lifespan, maxIdle);

// READ
ByteArray value;
bool found = cache.get(key, value);

// DELETE
ByteArray previousValue;
bool removed = cache.remove(key, &previousValue);

// PING
bool alive = cache.ping();
```

### In Progress
- GET_WITH_METADATA (Step 10)
- Version-based operations (REPLACE_IF_UNMODIFIED)

### Future
- Connection pooling
- TLS/SSL support
- Async operations
- Bulk operations

## Requirements

### Build Requirements
- **C++17** compatible compiler:
  - GCC 7+ (Linux)
  - Clang 6+ (Linux/macOS)
  - Visual Studio 2019+ / MSVC 19.20+ (Windows)
- **CMake** 3.15+
- **OpenSSL** 1.1.1+ or 3.x
- **Google Test** (for testing)

### Runtime Requirements
- Infinispan Server 15.0+ or 16.0+
- Docker (for integration tests)

## Building

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

### Unit Tests (150 tests)
```bash
cd build
./unit_tests

# Or via CTest
ctest -R UnitTests --output-on-failure
```

### Integration Tests (29 tests)
Requires Docker to run Infinispan server:
```bash
cd build

# Run all integration tests (starts server automatically)
./ping_integration_tests
./get_integration_tests  
./put_integration_tests
./remove_integration_tests

# Or via CTest
ctest -R IntegrationTests --output-on-failure
```

**Note**: Integration tests use GoogleTest global environment to manage Infinispan server lifecycle. The server is started once before all tests and stopped after completion.

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
│   ├── RemoteCache.h            # High-level API
│   ├── SCRAM.h                  # Authentication
│   ├── TopologyInfo.h           # Cluster tracking
│   └── ConsistentHash.h         # Smart routing
├── src/                         # Implementation
│   ├── codec/                   # Encoding/decoding
│   ├── transport/               # Network I/O
│   ├── auth/                    # SCRAM-SHA-256
│   ├── topology/                # Cluster tracking
│   ├── hash/                    # MurmurHash3 + consistent hashing
│   └── operations/              # Hot Rod operations (PING, GET, PUT, REMOVE)
├── tests/                       # Test suite
│   ├── unit/                    # 150 unit tests
│   └── integration/             # 29 integration tests
├── scripts/                     # Integration test helpers
│   ├── start_infinispan_noauth.sh
│   └── stop_infinispan.sh
├── test-configs/                # Server configurations
│   └── infinispan-noauth.xml   # Anonymous mode config
└── .github/workflows/           # CI/CD
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

## Quick Start Example

```cpp
#include "hotrod/RemoteCache.h"
#include <iostream>

using namespace hotrod;

int main() {
    // Connect to Infinispan server
    RemoteCache cache("localhost", 11222);
    cache.connect();

    // PUT key-value
    ByteArray key = {'m', 'y', 'k', 'e', 'y'};
    ByteArray value = {'m', 'y', 'v', 'a', 'l', 'u', 'e'};
    cache.put(key, value);

    // GET value
    ByteArray retrieved;
    if (cache.get(key, retrieved)) {
        std::string valueStr(retrieved.begin(), retrieved.end());
        std::cout << "Value: " << valueStr << std::endl;
    }

    // REMOVE key
    ByteArray previousValue;
    if (cache.remove(key, &previousValue)) {
        std::cout << "Key removed" << std::endl;
    }

    // Check server connectivity
    if (cache.ping()) {
        std::cout << "Server is alive" << std::endl;
    }

    cache.disconnect();
    return 0;
}
```

## Implementation Milestones

- ✅ **v0.1.0** (2026-06-11): Foundation complete (Steps 0-5)
- ✅ **v0.2.0** (2026-06-12): PING operation complete (Step 6)
- ✅ **v0.3.0** (2026-06-12): GET operation complete (Step 7)
- ✅ **v0.4.0** (2026-06-12): PUT operation complete (Step 8)
- ✅ **v0.5.0** (2026-06-12): REMOVE operation complete (Step 9) - **Full CRUD!**
- 🎯 **v0.6.0** (upcoming): Metadata operations (Step 10)

---

**Maintained by**: rigazilla  
**Status**: Active development  
**Current Step**: 10 (Metadata Operations) - **Full CRUD complete!** (Steps 0-9) ✅
