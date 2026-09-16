# Connection Multiplexing Design

**Goal**: Support concurrent operations on a single socket using message ID matching (like the Go client).

## Current Architecture (Blocking)

```cpp
// RemoteCache.cpp - Current implementation
ByteArray RemoteCache::sendRequest(const ByteArray& request) {
    connection_->send(request);        // Write request
    ByteArray response = readResponse(); // Block until response
    return response;
}
```

**Problem**: Only one operation at a time per connection.

## Target Architecture (Multiplexed)

Based on `infinispan-go-client/internal/connection/conn.go`:

### Key Components

1. **Message ID Multiplexing**
   - Each request gets unique message ID (atomic counter)
   - Response includes same message ID
   - Match responses to pending requests via map

2. **Asynchronous I/O**
   - Dedicated read thread per connection
   - Write mutex (multiple threads can send)
   - Pending requests tracked in thread-safe map

3. **Request/Response Channels**
   - Go uses channels; C++ will use condition variables + futures
   - Each pending request waits on its own promise/future

### Go Client Pattern (Reference)

```go
// conn.go lines 38-54
type Conn struct {
    addr               string
    netConn            net.Conn
    reader             *bufio.Reader
    writeMu            sync.Mutex        // Protects writes
    pending            sync.Map          // msgID → pendingEntry
    nextMsgID          atomic.Int64      // Atomic counter
    // ... other fields
}

type pendingEntry struct {
    op        operation.Operation
    ch        chan *response           // Response channel
    cacheName string
}

type response struct {
    value any
    err   error
}
```

### Execute Flow (Go)

```go
// conn.go lines 82-105
func (c *Conn) Execute(ctx context.Context, op operation.Operation) (any, error) {
    msgID := c.nextMsgID.Add(1)                          // 1. Get unique ID
    ch := make(chan *response, 1)                        // 2. Create response channel
    c.pending.Store(msgID, &pendingEntry{op: op, ch: ch})// 3. Register pending

    if err := c.writeRequest(msgID, op); err != nil {    // 4. Send request
        c.pending.Delete(msgID)
        return nil, err
    }

    select {                                              // 5. Wait for response
    case resp := <-ch:
        return resp.value, resp.err
    case <-ctx.Done():
        c.pending.Delete(msgID)
        return nil, ctx.Err()
    }
}
```

### Read Loop (Go)

```go
// conn.go lines 133-189
func (c *Conn) readLoop() {
    for {
        header, err := codec.ReadResponseHeader(c.reader)  // 1. Read response header
        if err != nil {
            c.closeAllPending(err)
            return
        }

        val, ok := c.pending.LoadAndDelete(header.MessageID) // 2. Find pending request
        if !ok {
            c.logger.Warn("no pending request for message", "messageId", header.MessageID)
            continue
        }
        entry := val.(*pendingEntry)

        result, err := entry.op.DecodeResponse(header.Status, c.reader) // 3. Decode body
        entry.ch <- &response{value: result, err: err}                  // 4. Send to waiter
    }
}
```

## C++17 Implementation Plan

### 1. New Classes

#### MultiplexedConnection.h

```cpp
#pragma once

#include "Types.h"
#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <atomic>
#include <memory>

namespace hotrod {

// Response for a single request
struct Response {
    ByteArray data;
    std::exception_ptr error;
};

// Pending request entry
struct PendingRequest {
    std::promise<Response> promise;
    std::string cacheName;
    uint8_t expectedOpcode;
};

/**
 * Multiplexed connection supporting concurrent operations.
 * 
 * Key features:
 * - One TCP socket shared by multiple concurrent operations
 * - Message ID matching (request → response)
 * - Dedicated read thread for async I/O
 * - Thread-safe request/response handling
 * 
 * Reference: infinispan-go-client/internal/connection/conn.go
 */
class MultiplexedConnection {
public:
    MultiplexedConnection(const std::string& host, uint16_t port);
    ~MultiplexedConnection();

    // Connect to server
    void connect();

    // Execute request and wait for response (thread-safe)
    std::future<Response> execute(const ByteArray& request, 
                                   uint64_t messageId,
                                   uint8_t expectedOpcode,
                                   const std::string& cacheName = "");

    // Close connection
    void close();

    bool isConnected() const;

private:
    std::string host_;
    uint16_t port_;
    int socket_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopReadLoop_{false};

    // Message ID generation (atomic for thread safety)
    std::atomic<uint64_t> nextMessageId_{0};

    // Pending requests: messageId → PendingRequest
    std::mutex pendingMutex_;
    std::map<uint64_t, std::unique_ptr<PendingRequest>> pending_;

    // Write mutex (only one thread writes at a time)
    std::mutex writeMutex_;

    // Read thread
    std::unique_ptr<std::thread> readThread_;

    // Read loop (runs in dedicated thread)
    void readLoop();

    // Send data (protected by writeMutex_)
    void sendData(const ByteArray& data);

    // Receive exact number of bytes
    ByteArray receiveData(size_t length);

    // Close all pending requests with error
    void closeAllPending(const std::string& errorMsg);
};

} // namespace hotrod
```

#### MultiplexedConnection.cpp (Key Methods)

```cpp
#include "MultiplexedConnection.h"
#include "Codec.h"
#include <stdexcept>

namespace hotrod {

MultiplexedConnection::MultiplexedConnection(const std::string& host, uint16_t port)
    : host_(host), port_(port), socket_(-1) {
}

MultiplexedConnection::~MultiplexedConnection() {
    close();
}

void MultiplexedConnection::connect() {
    if (connected_) {
        throw std::runtime_error("Already connected");
    }
    
    // TODO: TCP socket setup (same as current Connection.cpp)
    // ...
    
    connected_ = true;
    
    // Start read thread
    readThread_ = std::make_unique<std::thread>(&MultiplexedConnection::readLoop, this);
}

std::future<Response> MultiplexedConnection::execute(
    const ByteArray& request,
    uint64_t messageId,
    uint8_t expectedOpcode,
    const std::string& cacheName)
{
    if (!connected_) {
        throw std::runtime_error("Not connected");
    }

    // Create pending entry
    auto pending = std::make_unique<PendingRequest>();
    pending->cacheName = cacheName;
    pending->expectedOpcode = expectedOpcode;
    auto future = pending->promise.get_future();

    // Register pending request
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_[messageId] = std::move(pending);
    }

    // Send request (protected by write mutex)
    try {
        std::lock_guard<std::mutex> lock(writeMutex_);
        sendData(request);
    } catch (...) {
        // Remove pending on write error
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(messageId);
        throw;
    }

    return future;
}

void MultiplexedConnection::readLoop() {
    while (!stopReadLoop_ && connected_) {
        try {
            // 1. Read response header
            ByteArray magicByte = receiveData(1);
            if (magicByte[0] != 0xA1) {
                throw std::runtime_error("Invalid magic byte");
            }

            // 2. Read message ID (vLong)
            ByteArray buffer;
            buffer.push_back(magicByte[0]);
            
            while (true) {
                ByteArray byte = receiveData(1);
                buffer.push_back(byte[0]);
                if ((byte[0] & 0x80) == 0) break;  // vLong complete
            }

            // Decode message ID
            size_t offset = 1;  // Skip magic byte
            uint64_t messageId = Codec::readVLong(buffer, offset);

            // 3. Read opcode, status, topology marker
            ByteArray tail = receiveData(3);
            buffer.insert(buffer.end(), tail.begin(), tail.end());
            
            uint8_t opcode = tail[0];
            uint8_t status = tail[1];
            uint8_t topologyMarker = tail[2];

            // 4. Handle topology updates (if needed)
            // TODO: Read topology data if topologyMarker != 0

            // 5. Find pending request
            std::unique_ptr<PendingRequest> pending;
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                auto it = pending_.find(messageId);
                if (it == pending_.end()) {
                    // No pending request - log warning and continue
                    fprintf(stderr, "[WARN] No pending request for messageId=%lu\n", messageId);
                    continue;
                }
                pending = std::move(it->second);
                pending_.erase(it);
            }

            // 6. Validate opcode
            if (opcode != pending->expectedOpcode) {
                Response resp;
                resp.error = std::make_exception_ptr(
                    std::runtime_error("Opcode mismatch")
                );
                pending->promise.set_value(std::move(resp));
                continue;
            }

            // 7. Read response body (operation-specific)
            // For now, return the header. Caller will read body.
            Response resp;
            resp.data = buffer;
            pending->promise.set_value(std::move(resp));

        } catch (const std::exception& e) {
            closeAllPending(e.what());
            break;
        }
    }
}

void MultiplexedConnection::closeAllPending(const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto error = std::make_exception_ptr(std::runtime_error(errorMsg));
    
    for (auto& [msgId, pending] : pending_) {
        Response resp;
        resp.error = error;
        pending->promise.set_value(std::move(resp));
    }
    pending_.clear();
}

void MultiplexedConnection::close() {
    if (!connected_) return;
    
    stopReadLoop_ = true;
    
    // Close socket (will unblock receiveData in read thread)
    // TODO: platform-specific close
    
    connected_ = false;
    
    // Wait for read thread
    if (readThread_ && readThread_->joinable()) {
        readThread_->join();
    }
    
    closeAllPending("Connection closed");
}

} // namespace hotrod
```

### 2. Updated RemoteCache API

```cpp
// RemoteCache.h
class RemoteCache {
public:
    // Existing synchronous API (backward compatible)
    bool get(const ByteArray& key, ByteArray& value);
    bool put(const ByteArray& key, const ByteArray& value);
    
    // New async API (optional)
    std::future<bool> getAsync(const ByteArray& key);
    std::future<bool> putAsync(const ByteArray& key, const ByteArray& value);

private:
    std::unique_ptr<MultiplexedConnection> connection_;
    std::atomic<uint64_t> messageIdCounter_{0};
    
    // Helper: Execute operation and wait (for sync API)
    ByteArray executeSync(const ByteArray& request, uint8_t expectedOpcode);
};
```

### 3. Migration Strategy

**Phase 1**: Implement MultiplexedConnection (this design)
- New class alongside existing Connection
- Read loop in dedicated thread
- Thread-safe execute() method

**Phase 2**: Update RemoteCache
- Replace `Connection` with `MultiplexedConnection`
- Keep synchronous API (backward compatible)
- `get()`, `put()`, etc. call `executeSync()` internally

**Phase 3**: Add async API (optional)
- `getAsync()`, `putAsync()` return futures
- Allow concurrent operations from multiple threads

**Phase 4**: Remove old Connection class
- Once all tests pass with MultiplexedConnection

## Benefits

✅ **Concurrent operations**: Multiple threads can call `get()`, `put()` simultaneously  
✅ **Single socket per server**: Efficient resource usage  
✅ **Thread-safe**: Mutex + atomic operations  
✅ **Backward compatible**: Existing sync API still works  
✅ **Matches Go client**: Same architecture, proven pattern  

## Testing Strategy

1. **Unit tests**:
   - Concurrent GET operations (10 threads, 100 ops each)
   - Concurrent PUT operations
   - Mixed GET/PUT operations
   - Message ID uniqueness
   - Response matching correctness

2. **Integration tests**:
   - Concurrent operations against live Infinispan
   - Stress test: 1000 concurrent operations
   - Error handling: connection drops during pending ops

3. **Performance tests**:
   - Compare latency: current vs multiplexed
   - Throughput: ops/sec with 1 thread vs N threads

## Implementation Checklist

- [ ] Create MultiplexedConnection class
- [ ] Implement message ID matching
- [ ] Implement read loop thread
- [ ] Add write mutex
- [ ] Handle topology updates in read loop
- [ ] Unit tests for concurrent operations
- [ ] Update RemoteCache to use MultiplexedConnection
- [ ] Integration tests against Infinispan
- [ ] Update PROGRESS.md
- [ ] Documentation

## Open Questions

1. **Body reading**: Who reads the response body?
   - Option A: Read loop reads everything, stores in Response
   - Option B: Read loop reads header, caller reads body from socket
   - **Decision**: Option A (simpler, matches Go client)

2. **Error handling**: What if socket breaks during pending ops?
   - Go client: `closeAllPending(err)` sends error to all waiters
   - **Decision**: Same approach in C++

3. **Topology updates**: How to notify RemoteCache?
   - Go client: Callback function
   - **Decision**: Callback similar to Go

4. **Connection pool**: Still need one connection per server?
   - **Yes**: Multiplexing is per-connection, not per-cluster
   - Keep `connectionPool_` map in RemoteCache

## Reference

- Go client: `/home/rigazilla/git/infinispan-go-client/internal/connection/conn.go`
- Key lines: 38-54 (struct), 82-105 (Execute), 133-189 (readLoop)
- Java client: Uses Netty (event-driven I/O, more complex)
- This design: Closer to Go (simpler, easier to understand)
