# Multiplexing Design - Key Decisions Needed

## 1. Response Body Reading Strategy

**Problem**: Read loop needs to read entire response (header + body) before dispatching to waiter. But body format depends on operation type.

**Options:**

### Option A: Switch on Opcode in Read Loop
```cpp
void readLoop() {
    // Read header
    ByteArray body;
    switch (opcode) {
        case 0x04: body = readGetResponseBody(status); break;
        case 0x02: body = readPutResponseBody(status); break;
        // ... 30+ operations
    }
}
```
**Pros**: Centralized, read loop has full control  
**Cons**: Read loop becomes huge, needs to know all operations

### Option B: Pass Body Reader Lambda (Recommended)
```cpp
// Caller provides how to read body
auto bodyReader = [](uint8_t status, Connection* conn) -> ByteArray {
    if (status == 0x00) return conn->receiveByteArray();
    return {};
};

auto future = connection_->execute(request, msgId, 0x04, bodyReader);
```
**Pros**: Read loop stays simple, caller knows its operation  
**Cons**: Lambda passed with each request (small overhead)

### Option C: Operation Objects (Go Client Style)
```cpp
class Operation {
    virtual ByteArray readResponseBody(uint8_t status, Connection*) = 0;
};

connection_->execute(request, msgId, operationObject);
```
**Pros**: Clean OO design, matches Go client  
**Cons**: More complex, need operation class hierarchy

**Question**: Which option do you prefer?  
**Recommendation**: Option B (lambda) - simple and flexible

---

## 2. Response Data Structure

**Current design:**
```cpp
struct Response {
    ByteArray data;  // Ambiguous: header? body? both?
    std::exception_ptr error;
};
```

**Proposed:**
```cpp
struct Response {
    uint8_t status;           // 0x00=success, 0x01=not_found, etc.
    ByteArray body;           // Operation-specific body data
    std::exception_ptr error; // Set on comm errors (socket closed, etc.)
    std::optional<TopologyInfo> topologyUpdate;  // If topology changed
};
```

**Question**: Is this structure good, or do you want to change anything?

---

## 3. Topology Update Handling

**Options:**

### Option A: Callback (Go client style) - Recommended
```cpp
class MultiplexedConnection {
public:
    MultiplexedConnection(
        const std::string& host,
        uint16_t port,
        std::function<void(const TopologyInfo&)> onTopologyUpdate = nullptr
    );
};

// In RemoteCache
connection_ = std::make_unique<MultiplexedConnection>(
    host, port,
    [this](const TopologyInfo& topo) { this->topology_ = topo; }
);
```
**Pros**: Simple, matches Go, caller controls what to do  
**Cons**: Callback runs in read thread (need to be thread-safe)

### Option B: Store in Response
```cpp
struct Response {
    std::optional<TopologyInfo> topologyUpdate;
};

auto resp = future.get();
if (resp.topologyUpdate) {
    updateTopology(resp.topologyUpdate.value());
}
```
**Pros**: No callbacks, easier to reason about  
**Cons**: Every response check needed, even if no topology change

**Question**: Callback or store in response?  
**Recommendation**: Option A (callback) - matches proven Go design

---

## 4. Error Handling

**When socket breaks during pending operations:**

**Go client approach** (from conn.go:210-230):
```go
func (c *Conn) closeAllPending(err error) {
    c.pending.Range(func(key, value any) bool {
        entry := value.(*pendingEntry)
        entry.ch <- &response{err: err}  // Send error to all waiters
        c.pending.Delete(key)
        return true
    })
}
```

**C++ equivalent:**
```cpp
void MultiplexedConnection::closeAllPending(const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto error = std::make_exception_ptr(std::runtime_error(errorMsg));
    
    for (auto& [msgId, pending] : pending_) {
        Response resp;
        resp.error = error;
        try {
            pending->promise.set_value(std::move(resp));
        } catch (const std::future_error&) {
            // Promise already satisfied, ignore
        }
    }
    pending_.clear();
}
```

**Question**: Is this error handling approach acceptable?  
**Alternative**: Use `std::promise::set_exception(error)` instead of `Response.error`?

---

## 5. Thread Lifetime Management

**Read thread lifecycle:**

```cpp
MultiplexedConnection::~MultiplexedConnection() {
    close();
}

void MultiplexedConnection::close() {
    stopReadLoop_ = true;
    
    // Close socket (unblocks receive() in read thread)
    ::close(socket_);
    
    // Wait for read thread to finish
    if (readThread_ && readThread_->joinable()) {
        readThread_->join();
    }
    
    closeAllPending("Connection closed");
}
```

**Problem**: What if `receive()` is blocked and `close(socket_)` doesn't unblock it immediately?

**Options:**
- **A**: Trust that `close()` unblocks `recv()` (POSIX behavior)
- **B**: Set socket timeout (SO_RCVTIMEO) so read loop wakes periodically
- **C**: Use `shutdown()` before `close()` for immediate unblock

**Question**: Which approach for graceful shutdown?  
**Recommendation**: Option C (shutdown then close) - most reliable

---

## 6. Message ID Generation

**Current design:**
```cpp
std::atomic<uint64_t> nextMessageId_{0};

std::future<Response> execute(...) {
    uint64_t msgId = nextMessageId_++;  // Atomic increment
    // ...
}
```

**Wait, who generates message ID?**

**Option A: Connection generates (current design)**
```cpp
// In MultiplexedConnection::execute()
uint64_t msgId = nextMessageId_++;
```
**Pros**: Simple, connection owns it  
**Cons**: Request already has message ID encoded, wasteful

**Option B: Caller generates (matches current RemoteCache)**
```cpp
// In RemoteCache::get()
uint64_t msgId = nextMessageId();  // RemoteCache generates
ByteArray request = encodeGetRequest(msgId, key);
connection_->execute(request, msgId, bodyReader);
```
**Pros**: Request already has correct ID  
**Cons**: Caller must track IDs, more complex API

**Question**: Who generates message IDs - Connection or RemoteCache?

**Current C++ client**: RemoteCache generates (see RemoteCache.cpp:38-40)  
**Go client**: Connection generates (see conn.go:87)

**Recommendation**: Follow Go - Connection generates. Simpler API.

But this means **changing request encoding**:

```cpp
// Instead of:
ByteArray request = encodeGetRequest(msgId, key);
connection_->send(request);

// We need:
ByteArray requestBody = encodeGetRequestBody(key);  // No msgId yet
connection_->execute(requestBody, expectedOpcode, bodyReader);
// Connection adds msgId to request before sending
```

**Is this acceptable?**

---

## 7. Connection Pool Integration

**Current RemoteCache has:**
```cpp
std::map<std::string, std::unique_ptr<Connection>> connectionPool_;
```

**With multiplexing:**
```cpp
std::map<std::string, std::unique_ptr<MultiplexedConnection>> connectionPool_;
```

Each MultiplexedConnection has its own:
- Read thread
- Message ID counter  
- Pending requests map

**Question**: Keep one connection per server, or allow multiple connections per server?

**Recommendation**: Keep one connection per server (simpler, matches Go client)

---

## Summary - Decisions Needed

| # | Decision | Options | Recommendation |
|---|----------|---------|----------------|
| 1 | Body reading | Switch / Lambda / Operation objects | **Lambda** (simple) |
| 2 | Response structure | Current / Enhanced | **Enhanced** (status + body + topo) |
| 3 | Topology updates | Callback / Store in response | **Callback** (matches Go) |
| 4 | Error handling | set_value(error) / set_exception | **set_value(error)** (uniform) |
| 5 | Thread shutdown | close / timeout / shutdown+close | **shutdown+close** (reliable) |
| 6 | Message ID generation | Connection / RemoteCache | **Connection** (matches Go, simpler API) |
| 7 | Connection pool | One per server / Multiple | **One per server** (simpler) |

**Most critical decision: #1 (body reading)** - affects the entire API design.

**Please review and let me know:**
- Do you agree with the recommendations?
- Any concerns or alternative ideas?
- Ready to proceed with implementation?
