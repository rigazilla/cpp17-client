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

## User-Decided Retry (`retry` example)

`retry.cpp` demonstrates Step 11b/11c — **the client never retries on its own**.
A failure surfaces as a typed `HotRodClientException` carrying the facts you need
(which phase it failed in, which nodes were already tried); you opt into a retry
explicitly, on the failure path, via `cache.excluding(e)`:

```cpp
std::vector<ServerAddress> excluded;      // nodes tried so far
for (int attempt = 1; ; ++attempt) {
    try {
        return excluded.empty()
            ? cache.get(key).get()
            : cache.excluding(excluded).get(key).get();
    } catch (const HotRodClientException& e) {
        if (!isTransient(e)) throw;       // permanent — give up
        // ... your idempotency decision (outcomeUncertain(e)) for writes ...
        excluded = e.triedNodes;          // avoid tried nodes next time
    }
}
```

Why user-decided? Only the caller knows whether replaying *this* operation is
safe. `isTransient(e)` answers "is a retry worthwhile?"; `outcomeUncertain(e)`
answers "might it already have applied?" — you `AND` the second with your own
knowledge of the op before replaying a non-idempotent write.

> **The `proxyToNonOwner` flag does NOT let you skip the catch block.**
> `proxyToNonOwner=true` (the default) only widens the *connection-selection*
> candidate pool for a **single** dispatch: if no owner is reachable *before the
> request is sent*, selection falls through to another node that proxies to the
> owner. But one operation still executes on exactly **one** node, so any failure
> *after* the request is sent (dropped connection, server ERROR, command timeout)
> still comes back to you as an exception. The flag changes where the first
> attempt is *sent*; it does not make retry automatic.

**Keyless operations retry the same way (Step 11c).** `ping()` has no key and so
no owners; it routes to any server in topology order, automatically fails over to
the next server *before send*, and surfaces after-send/server errors to you just
like a keyed op — so you retry it with `cache.excluding(e).ping()`:

```cpp
std::vector<ServerAddress> excluded;
for (int attempt = 1; ; ++attempt) {
    try {
        if (excluded.empty()) cache.ping().get();
        else                  cache.excluding(excluded).ping().get();
        return;
    } catch (const HotRodClientException& e) {
        if (!isTransient(e)) throw;   // ping is idempotent — always safe to replay
        excluded = e.triedNodes;
    }
}
```

The only visible difference from a keyed op: a keyless exhaustion reports
`ownersExhausted == false` (there are no owners). The example calls
`pingWithRetry()` right after connecting, since `ping` is typically the first op —
often before any topology has arrived, in which case it falls back to the seed
connection.

Build it alongside `quickstart` (both targets are in `CMakeLists.txt`):

```bash
cd build && cmake .. && make        # builds quickstart and retry
./retry
```

Run it against a **cluster** and kill an owner node mid-run to actually exercise
the retry path (the same scenarios are covered by
`tests/integration/RetryViewIntegrationTest.cpp` for keyed ops and
`tests/integration/PingRetryIntegrationTest.cpp` for keyless `ping`).

## Files

- `quickstart.cpp` - Basic PUT/GET/REMOVE example
- `retry.cpp` - User-decided retry loop, keyed + keyless `ping` (Step 11b/11c)
- `CMakeLists.txt` - Build configuration (builds both examples)
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
