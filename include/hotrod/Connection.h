#pragma once

#include "Types.h"
#include <string>
#include <cstdint>

namespace hotrod {

// Portable socket handle type. On Windows a SOCKET is an unsigned
// pointer-sized handle (UINT_PTR); on POSIX it is an int file descriptor.
// Using the right width avoids truncating the handle (MSVC C4244).
#ifdef _WIN32
using socket_t = uintptr_t;
#else
using socket_t = int;
#endif

// Portable invalid-socket sentinel: equals INVALID_SOCKET on Windows and
// -1 on POSIX, without a signed/unsigned conversion warning at each use.
constexpr socket_t INVALID_SOCK = static_cast<socket_t>(-1);

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
    socket_t socket_;  // Socket descriptor (platform-specific)
};

} // namespace hotrod
