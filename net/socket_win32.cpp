#include "socket_win32.hpp"
#include <cstring>

namespace browser::net {

int Win32TCPSocket::wsa_refcount_ = 0;

Result<void> Win32TCPSocket::ensure_wsa_started() {
    if (wsa_refcount_ > 0) {
        wsa_refcount_++;
        return {};
    }
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ret != 0) {
        return std::string("WSAStartup failed");
    }
    wsa_refcount_ = 1;
    return {};
}

void Win32TCPSocket::wsa_cleanup() {
    if (wsa_refcount_ > 0) {
        wsa_refcount_--;
        if (wsa_refcount_ == 0) {
            WSACleanup();
        }
    }
}

Win32TCPSocket::Win32TCPSocket() : handle_(INVALID_SOCKET) {
    ensure_wsa_started();
}

Win32TCPSocket::~Win32TCPSocket() {
    ensure_closed();
    wsa_cleanup();
}

void Win32TCPSocket::ensure_closed() {
    if (handle_ != INVALID_SOCKET) {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
    }
}

Result<void> Win32TCPSocket::connect(const std::string& host, u16 port) {
    ensure_closed();

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    auto port_str = std::to_string(port);
    int ret = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0 || result == nullptr) {
        return std::string("getaddrinfo failed: " + host);
    }

    handle_ = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (handle_ == INVALID_SOCKET) {
        ::freeaddrinfo(result);
        return std::string("socket creation failed");
    }

    ret = ::connect(handle_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    ::freeaddrinfo(result);
    if (ret == SOCKET_ERROR) {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
        return std::string("connect failed to " + host);
    }

    return {};
}

Result<void> Win32TCPSocket::connect_ip(const IPv4Address& addr, u16 port) {
    ensure_closed();

    handle_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket creation failed");
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    u32 ip = (static_cast<u32>(addr.octets[0]) << 24) |
             (static_cast<u32>(addr.octets[1]) << 16) |
             (static_cast<u32>(addr.octets[2]) << 8) |
              static_cast<u32>(addr.octets[3]);
    server_addr.sin_addr.s_addr = htonl(ip);

    int ret = ::connect(handle_, reinterpret_cast<struct sockaddr*>(&server_addr),
                        static_cast<int>(sizeof(server_addr)));
    if (ret == SOCKET_ERROR) {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
        return std::string("connect_ip failed");
    }

    return {};
}

Result<u32> Win32TCPSocket::send(const u8* data, u32 len) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket not connected");
    }
    int ret = ::send(handle_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    if (ret == SOCKET_ERROR) {
        return std::string("send failed");
    }
    return static_cast<u32>(ret);
}

Result<void> Win32TCPSocket::send_all(const u8* data, u32 len) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket not connected");
    }
    u32 total = 0;
    while (total < len) {
        int ret = ::send(handle_, reinterpret_cast<const char*>(data + total),
                         static_cast<int>(len - total), 0);
        if (ret == SOCKET_ERROR) {
            return std::string("send_all failed");
        }
        total += static_cast<u32>(ret);
    }
    return {};
}

Result<u32> Win32TCPSocket::receive(u8* buf, u32 len) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket not connected");
    }
    int ret = ::recv(handle_, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (ret == SOCKET_ERROR) {
        return std::string("recv failed");
    }
    return static_cast<u32>(ret);
}

Result<void> Win32TCPSocket::receive_all(u8* buf, u32 len) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket not connected");
    }
    u32 total = 0;
    while (total < len) {
        int ret = ::recv(handle_, reinterpret_cast<char*>(buf + total),
                         static_cast<int>(len - total), 0);
        if (ret == SOCKET_ERROR) {
            return std::string("receive_all failed");
        }
        if (ret == 0) {
            return std::string("connection closed before receiving all data");
        }
        total += static_cast<u32>(ret);
    }
    return {};
}

Result<void> Win32TCPSocket::set_read_timeout(u32 ms) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("socket not connected");
    }
    DWORD timeout = ms;
    int ret = ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    if (ret == SOCKET_ERROR) {
        return std::string("setsockopt SO_RCVTIMEO failed");
    }
    return {};
}

void Win32TCPSocket::close() {
    ensure_closed();
}

bool Win32TCPSocket::is_connected() const {
    return handle_ != INVALID_SOCKET;
}

// --- Win32UDPSocket ---

Win32UDPSocket::Win32UDPSocket() : handle_(INVALID_SOCKET) {
    Win32TCPSocket::ensure_wsa_started();
    handle_ = ::socket(AF_INET, SOCK_DGRAM, 0);
}

Win32UDPSocket::~Win32UDPSocket() {
    ensure_closed();
    Win32TCPSocket::wsa_cleanup();
}

void Win32UDPSocket::ensure_closed() {
    if (handle_ != INVALID_SOCKET) {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
    }
}

Result<void> Win32UDPSocket::send(const u8* data, u32 len, const IPv4Address& dest, u16 port) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("UDP socket not created");
    }
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    u32 ip = (static_cast<u32>(dest.octets[0]) << 24) |
             (static_cast<u32>(dest.octets[1]) << 16) |
             (static_cast<u32>(dest.octets[2]) << 8) |
              static_cast<u32>(dest.octets[3]);
    dest_addr.sin_addr.s_addr = htonl(ip);

    int ret = ::sendto(handle_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
                       reinterpret_cast<struct sockaddr*>(&dest_addr),
                       static_cast<int>(sizeof(dest_addr)));
    if (ret == SOCKET_ERROR) {
        return std::string("sendto failed");
    }
    return {};
}

Result<u32> Win32UDPSocket::receive(u8* buf, u32 len, IPv4Address* sender, u16* sender_port) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("UDP socket not created");
    }
    struct sockaddr_in from_addr = {};
    int from_len = static_cast<int>(sizeof(from_addr));
    int ret = ::recvfrom(handle_, reinterpret_cast<char*>(buf), static_cast<int>(len), 0,
                         reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
    if (ret == SOCKET_ERROR) {
        return std::string("recvfrom failed");
    }
    if (sender) {
        u32 ip = ntohl(from_addr.sin_addr.s_addr);
        sender->octets[0] = static_cast<u8>((ip >> 24) & 0xFF);
        sender->octets[1] = static_cast<u8>((ip >> 16) & 0xFF);
        sender->octets[2] = static_cast<u8>((ip >> 8) & 0xFF);
        sender->octets[3] = static_cast<u8>(ip & 0xFF);
    }
    if (sender_port) {
        *sender_port = ntohs(from_addr.sin_port);
    }
    return static_cast<u32>(ret);
}

Result<void> Win32UDPSocket::set_read_timeout(u32 ms) {
    if (handle_ == INVALID_SOCKET) {
        return std::string("UDP socket not created");
    }
    DWORD timeout = ms;
    int ret = ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    if (ret == SOCKET_ERROR) {
        return std::string("setsockopt SO_RCVTIMEO failed");
    }
    return {};
}

void Win32UDPSocket::close() {
    ensure_closed();
}

} // namespace browser::net
