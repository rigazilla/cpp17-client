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
    : host_(host), port_(port), connected_(false), socket_(-1) {
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

        // Attempt to connect
        if (::connect(socket_, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen)) == 0) {
            connected_ = true;
            break;  // Success
        }

        // Close and try next address
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = -1;
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

void Connection::close() {
    if (!connected_) {
        return;
    }

#ifdef _WIN32
    closesocket(socket_);
#else
    ::close(socket_);
#endif

    socket_ = -1;
    connected_ = false;
}

} // namespace hotrod
