#pragma once

#include "Types.h"
#include <string>
#include <cstdint>

namespace hotrod {

/**
 * TCP connection to Infinispan server.
 * Cross-platform: POSIX sockets (Linux/Unix) and Winsock2 (Windows)
 */
class Connection {
public:
    Connection(const std::string& host, uint16_t port);
    ~Connection();

    // Connect to server
    void connect();

    // Send data
    void send(const ByteArray& data);

    // Receive data
    ByteArray receive(size_t length);

    // Shutdown connection (interrupts blocking recv)
    void shutdown();

    // Close connection
    void close();

    bool isConnected() const { return connected_; }

    // Helper methods for body parsers (used by MultiplexedConnection)
    VInt receiveVInt();
    VLong receiveVLong();
    ByteArray receiveByteArray();  // lp_bytes (vInt length + bytes)
    std::string receiveString();   // lp_string (vInt length + UTF-8)
    EntryMetadata receiveMetadata(); // entry_metadata (Protocol 4.0+)

private:
    std::string host_;
    uint16_t port_;
    bool connected_;
    int socket_;  // Socket descriptor (platform-specific)
};

} // namespace hotrod
