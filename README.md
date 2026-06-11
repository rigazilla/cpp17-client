# Infinispan Hot Rod Client - C++17

A cross-platform C++17 implementation of the Infinispan Hot Rod protocol client.

## Status

🚧 **In Development** - Following [hotrod-foundry](../hotrod-foundry) implementation roadmap.

**Current Progress**: Steps 0-5 COMPLETE ✅ - Foundation ready! See [PROGRESS.md](PROGRESS.md)

## Features

### Implemented (v0.1.0 - in progress)
- ✅ Cross-platform support (Linux + Windows)
- ✅ Wire format primitives (vInt, vLong, strings, byte arrays)
- ✅ Protocol 4.0 headers (complete spec)
- ✅ SCRAM-SHA-256 authentication (RFC 5802)
- ✅ Topology awareness (cluster failover, load balancing)
- ✅ Consistent hashing (MurmurHash3, smart routing)
- ⏳ PING operation (in progress)

### Future
- GET/PUT operations
- REMOVE operation
- Connection pooling
- TLS/SSL support

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
- Docker (for integration tests with Testcontainers)

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

### Unit Tests
```bash
cd build
ctest -R unit --output-on-failure
```

### Integration Tests (TODO)
Requires Docker for Testcontainers:
```bash
cd build
ctest -R integration --output-on-failure
```

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
│   └── Connection.h             # TCP connection
├── src/                         # Implementation
│   ├── codec/                   # Encoding/decoding
│   ├── transport/               # Network I/O
│   ├── auth/                    # Authentication
│   ├── topology/                # Cluster tracking
│   └── operations/              # Hot Rod operations
├── tests/                       # Test suite
│   ├── unit/                    # Unit tests
│   └── integration/             # Integration tests
├── .github/workflows/           # CI/CD
└── docs/examples/               # Usage examples
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

---

**Maintained by**: rigazilla  
**Status**: Active development  
**Current Step**: 6 (PING Operation) - Foundation complete (Steps 0-5) ✅
