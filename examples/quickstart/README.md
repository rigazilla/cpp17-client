# Hot Rod C++ Client - Quickstart Example

A simple example demonstrating basic operations with the Hot Rod C++ client.

## What This Example Does

The quickstart example shows how to:
- Connect to an Infinispan server
- **PUT**: Store key-value pairs in the cache
- **GET**: Retrieve values by key
- **REMOVE**: Delete entries from the cache

## Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.12 or higher
- Docker (for running Infinispan server)

## Quick Start

### 1. Build the Hot Rod Client Library

First, build the main library (if not already built):

```bash
cd ../../build
cmake ..
make
```

### 2. Start Infinispan Server

Start a local Infinispan server using Docker:

```bash
./start-server.sh
```

This will:
- Start Infinispan 16.2 in a Docker container
- Listen on `localhost:11222`
- Create a distributed cache named `quickstart-cache`
- No authentication required for quickstart

**Web Console**: http://localhost:11222/console

### 3. Build the Quickstart Example

```bash
mkdir build
cd build
cmake ..
make
```

### 4. Run the Example

```bash
./quickstart
```

Expected output:
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
GET: nonexistent = <not found>

--- REMOVE Operations ---
REMOVE: language deleted successfully
GET: language = <not found (after delete)>

--- Summary ---
Remaining entries in cache:
  greeting = Hello, Infinispan!
  version = 1.0.0

Disconnected from server.

=== Quickstart Complete ===
```

### 5. Stop the Server

When done:

```bash
./stop-server.sh
```

## Code Overview

The example demonstrates the basic Hot Rod API:

```cpp
// Connect to server
RemoteCache cache("localhost", 11222);
cache.connect();

// PUT: Store a value
ByteArray key(keyStr.begin(), keyStr.end());
ByteArray value(valueStr.begin(), valueStr.end());
cache.put(key, value);

// GET: Retrieve a value
ByteArray retrievedValue;
if (cache.get(key, retrievedValue)) {
    // Key found
}

// REMOVE: Delete an entry
cache.remove(key);

// Disconnect
cache.disconnect();
```

## Files

- `quickstart.cpp` - Main example code
- `CMakeLists.txt` - Build configuration
- `start-server.sh` - Script to start Infinispan server
- `stop-server.sh` - Script to stop Infinispan server
- `README.md` - This file

## Troubleshooting

**Server won't start:**
- Check if port 11222 is already in use: `lsof -i :11222`
- Check Docker is running: `docker ps`

**Build errors:**
- Make sure the main library is built first: `cd ../../build && make`
- Check CMake output for missing dependencies

**Connection refused:**
- Verify server is running: `docker ps | grep infinispan-quickstart`
- Wait a few more seconds for server startup
- Check server logs: `docker logs infinispan-quickstart`

## Next Steps

- Explore hash-aware routing: See `ClientIntelligence::HASH_DISTRIBUTION_AWARE`
- Try different cache configurations
- Read the full API documentation in `../../include/hotrod/`

## Learn More

- [Infinispan Documentation](https://infinispan.org/documentation/)
- [Hot Rod Protocol](https://infinispan.org/docs/stable/titles/hotrod_protocol/hotrod_protocol.html)
