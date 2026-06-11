#pragma once

#include "Types.h"
#include <string>

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

    // Close connection
    void close();

    bool isConnected() const { return connected_; }

private:
    std::string host_;
    uint16_t port_;
    bool connected_;
    int socket_;  // Socket descriptor (platform-specific)
};

} // namespace hotrod
