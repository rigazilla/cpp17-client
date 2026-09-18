#include "hotrod/Connection.h"
#include <stdexcept>
#include <cstring>

// Platform-specific includes
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace hotrod {

// Platform-specific initialization
#ifdef _WIN32
    struct WinsockInitializer {
        WinsockInitializer() {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                throw std::runtime_error("WSAStartup failed");
            }
        }
        ~WinsockInitializer() {
            WSACleanup();
        }
    };
    static WinsockInitializer winsockInit;  // Global initializer
#endif

Connection::Connection(const std::string& host, uint16_t port)
    : host_(host), port_(port), connected_(false), socket_(INVALID_SOCK) {
}

Connection::~Connection() {
    if (connected_) {
        close();
    }
}

void Connection::connect() {
    if (connected_) {
        throw std::runtime_error("Already connected");
    }

    // Resolve hostname
    struct addrinfo hints, *result = nullptr, *rp = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP socket
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port_);
    int status = getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &result);
    if (status != 0) {
#ifdef _WIN32
        throw std::runtime_error("getaddrinfo failed: " + std::to_string(WSAGetLastError()));
#else
        throw std::runtime_error("getaddrinfo failed: " + std::string(gai_strerror(status)));
#endif
    }

    // Try each address until we successfully connect
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        socket_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

#ifdef _WIN32
        if (socket_ == INVALID_SOCKET) {
            continue;
        }
#else
        if (socket_ == -1) {
            continue;
        }
#endif

        // Set socket to non-blocking mode for connect with timeout
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(socket_, FIONBIO, &mode);
#else
        int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

        // Attempt to connect (non-blocking)
        int connectResult = ::connect(socket_, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen));

        if (connectResult == 0) {
            // Immediate success (rare for non-blocking)
            connected_ = true;
        } else {
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) {
#else
            if (errno == EINPROGRESS) {
#endif
                // Connection in progress, wait with timeout (5 seconds)
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(socket_, &writefds);

                struct timeval timeout;
                timeout.tv_sec = 5;   // 5 second timeout
                timeout.tv_usec = 0;

                int selectResult = select(static_cast<int>(socket_ + 1), nullptr, &writefds, nullptr, &timeout);

                if (selectResult > 0) {
                    // Check if connection succeeded
                    int so_error;
                    socklen_t len = sizeof(so_error);
                    getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len);

                    if (so_error == 0) {
                        connected_ = true;
                    }
                }
            }
        }

        if (connected_) {
            // Set socket back to blocking mode
#ifdef _WIN32
            mode = 0;
            ioctlsocket(socket_, FIONBIO, &mode);
#else
            flags = fcntl(socket_, F_GETFL, 0);
            fcntl(socket_, F_SETFL, flags & ~O_NONBLOCK);
#endif
            break;  // Success
        }

        // Close and try next address
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = INVALID_SOCK;
    }

    freeaddrinfo(result);

    if (!connected_) {
        throw std::runtime_error("Failed to connect to " + host_ + ":" + std::to_string(port_));
    }
}

void Connection::send(const ByteArray& data) {
    if (!connected_) {
        throw std::runtime_error("Not connected");
    }

    size_t totalSent = 0;
    while (totalSent < data.size()) {
        const char* ptr = reinterpret_cast<const char*>(data.data()) + totalSent;
        size_t remaining = data.size() - totalSent;

#ifdef _WIN32
        int sent = ::send(socket_, ptr, static_cast<int>(remaining), 0);
        if (sent == SOCKET_ERROR) {
            throw std::runtime_error("send failed: " + std::to_string(WSAGetLastError()));
        }
#else
        ssize_t sent = ::send(socket_, ptr, remaining, 0);
        if (sent == -1) {
            throw std::runtime_error("send failed: " + std::string(strerror(errno)));
        }
#endif

        totalSent += sent;
    }
}

ByteArray Connection::receive(size_t length) {
    if (!connected_) {
        throw std::runtime_error("Not connected");
    }

    ByteArray buffer(length);
    size_t totalReceived = 0;

    while (totalReceived < length) {
        char* ptr = reinterpret_cast<char*>(buffer.data()) + totalReceived;
        size_t remaining = length - totalReceived;

#ifdef _WIN32
        int received = ::recv(socket_, ptr, static_cast<int>(remaining), 0);
        if (received == SOCKET_ERROR) {
            throw std::runtime_error("recv failed: " + std::to_string(WSAGetLastError()));
        } else if (received == 0) {
            throw std::runtime_error("Connection closed by peer");
        }
#else
        ssize_t received = ::recv(socket_, ptr, remaining, 0);
        if (received == -1) {
            throw std::runtime_error("recv failed: " + std::string(strerror(errno)));
        } else if (received == 0) {
            throw std::runtime_error("Connection closed by peer");
        }
#endif

        totalReceived += received;
    }

    return buffer;
}

void Connection::shutdown() {
    if (!connected_ || socket_ == INVALID_SOCK) {
        return;
    }

    // Shutdown socket to interrupt blocking recv() calls
    // SHUT_RDWR = 2 on both POSIX and Windows
#ifdef _WIN32
    ::shutdown(socket_, SD_BOTH);
#else
    ::shutdown(socket_, SHUT_RDWR);
#endif
}

void Connection::close() {
    if (!connected_) {
        return;
    }

#ifdef _WIN32
    closesocket(socket_);
#else
    ::close(socket_);
#endif

    socket_ = INVALID_SOCK;
    connected_ = false;
}

// Helper methods for body parsers

VInt Connection::receiveVInt() {
    ByteArray firstByte = receive(1);
    VInt value = firstByte[0] & 0x7F;

    for (int shift = 7; (firstByte[0] & 0x80) != 0; shift += 7) {
        ByteArray nextByte = receive(1);
        value |= static_cast<VInt>(nextByte[0] & 0x7F) << shift;
        firstByte[0] = nextByte[0];  // Update for continuation check
    }

    return value;
}

VLong Connection::receiveVLong() {
    ByteArray firstByte = receive(1);
    VLong value = firstByte[0] & 0x7F;

    for (int shift = 7; (firstByte[0] & 0x80) != 0; shift += 7) {
        ByteArray nextByte = receive(1);
        value |= static_cast<VLong>(nextByte[0] & 0x7F) << shift;
        firstByte[0] = nextByte[0];
    }

    return value;
}

ByteArray Connection::receiveByteArray() {
    // Read vInt length
    VInt length = receiveVInt();

    // Read bytes
    if (length > 0) {
        return receive(length);
    }
    return {};
}

std::string Connection::receiveString() {
    ByteArray bytes = receiveByteArray();
    return std::string(bytes.begin(), bytes.end());
}

EntryMetadata Connection::receiveMetadata() {
    EntryMetadata metadata;

    // Read flag byte
    uint8_t flag = receive(1)[0];

    // Read created + lifespan if not infinite (flag bit 0 == 0)
    if ((flag & 0x01) == 0) {
        // created (s8 - signed 64-bit big-endian)
        ByteArray createdBytes = receive(8);
        int64_t created = 0;
        for (int i = 0; i < 8; i++) {
            created = (created << 8) | createdBytes[i];
        }
        metadata.created = created;

        // lifespan (vint)
        metadata.lifespan = receiveVInt();
    }

    // Read last_used + max_idle if not infinite (flag bit 1 == 0)
    if ((flag & 0x02) == 0) {
        // last_used (s8 - signed 64-bit big-endian)
        ByteArray lastUsedBytes = receive(8);
        int64_t lastUsed = 0;
        for (int i = 0; i < 8; i++) {
            lastUsed = (lastUsed << 8) | lastUsedBytes[i];
        }
        metadata.lastUsed = lastUsed;

        // max_idle (vint)
        metadata.maxIdle = receiveVInt();
    }

    // Read entry_version (s8 - always present)
    ByteArray versionBytes = receive(8);
    int64_t version = 0;
    for (int i = 0; i < 8; i++) {
        version = (version << 8) | versionBytes[i];
    }
    metadata.version = version;

    return metadata;
}

} // namespace hotrod
