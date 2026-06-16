#include "socket_win32.hpp"
#include "../async/executor.hpp"
#include <cstring>

namespace browser::net {

int Win32TCPSocket::wsa_refcount_ = 0;
bool Win32TCPSocket::iocp_initialized_ = false;

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

    if (!iocp_initialized_) {
        auto& iocp = IOCP::global();
        auto r = iocp.create(0, 0);
        if (r.is_err()) {
            WSACleanup();
            return std::string("IOCP create: ") + r.unwrap_err();
        }
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        auto wr = iocp.start_workers(static_cast<u32>(sysinfo.dwNumberOfProcessors * 2));
        if (wr.is_err()) {
            iocp.close();
            WSACleanup();
            return std::string("IOCP workers: ") + wr.unwrap_err();
        }
        iocp_initialized_ = true;
    }
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

Win32TCPSocket::Win32TCPSocket() : handle_(INVALID_SOCKET), iocp_(nullptr), connect_ex_func_(nullptr) {
    ensure_wsa_started();
    iocp_ = &IOCP::global();
}

Win32TCPSocket::~Win32TCPSocket() {
    ensure_closed();
    wsa_cleanup();
}

void Win32TCPSocket::ensure_closed() {
    if (handle_ != INVALID_SOCKET) {
        ::shutdown(handle_, SD_BOTH);
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
    }
}

Result<void> Win32TCPSocket::ensure_iocp() {
    if (!iocp_ || !iocp_->is_valid()) {
        return std::string("IOCP not available");
    }
    return {};
}

Result<void> Win32TCPSocket::load_connect_ex() {
    if (connect_ex_func_) return {};
    if (handle_ == INVALID_SOCKET) return std::string("socket not created");
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    int ret = WSAIoctl(handle_, SIO_GET_EXTENSION_FUNCTION_POINTER,
                       &guid, sizeof(guid),
                       &connect_ex_func_, sizeof(connect_ex_func_),
                       &bytes, nullptr, nullptr);
    if (ret == SOCKET_ERROR) {
        return std::string("WSAIoctl ConnectEx failed");
    }
    return {};
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

    handle_ = ::WSASocket(result->ai_family, result->ai_socktype, result->ai_protocol,
                          nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == INVALID_SOCKET) {
        ::freeaddrinfo(result);
        return std::string("WSASocket failed");
    }

    auto iocp_r = iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
    if (iocp_r.is_err()) {
        ::freeaddrinfo(result);
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
        return std::string("IOCP associate: ") + iocp_r.unwrap_err();
    }

    ret = ::connect(handle_, result->ai_addr, static_cast<int>(result->ai_addrlen));
    ::freeaddrinfo(result);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            ::closesocket(handle_);
            handle_ = INVALID_SOCKET;
            return std::string("connect failed to " + host);
        }
        fd_set fd;
        FD_ZERO(&fd);
        FD_SET(handle_, &fd);
        struct timeval tv = {5, 0};
        ret = ::select(0, nullptr, &fd, nullptr, &tv);
        if (ret <= 0) {
            ::closesocket(handle_);
            handle_ = INVALID_SOCKET;
            return std::string("connect timeout for " + host);
        }
    }

    return {};
}

Result<void> Win32TCPSocket::connect_ip(const IPv4Address& addr, u16 port) {
    ensure_closed();

    handle_ = ::WSASocket(AF_INET, SOCK_STREAM, 0,
                          nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == INVALID_SOCKET) {
        return std::string("WSASocket failed");
    }

    auto iocp_r = iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
    if (iocp_r.is_err()) {
        ::closesocket(handle_);
        handle_ = INVALID_SOCKET;
        return std::string("IOCP associate: ") + iocp_r.unwrap_err();
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
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            ::closesocket(handle_);
            handle_ = INVALID_SOCKET;
            return std::string("connect_ip failed");
        }
        fd_set fd;
        FD_ZERO(&fd);
        FD_SET(handle_, &fd);
        struct timeval tv = {5, 0};
        ret = ::select(0, nullptr, &fd, nullptr, &tv);
        if (ret <= 0) {
            ::closesocket(handle_);
            handle_ = INVALID_SOCKET;
            return std::string("connect_ip timeout");
        }
    }

    return {};
}

Result<u32> Win32TCPSocket::send(const u8* data, u32 len) {
    if (handle_ == INVALID_SOCKET) return std::string("socket not connected");
    WSABUF buf = { len, reinterpret_cast<char*>(const_cast<u8*>(data)) };
    DWORD sent = 0;
    int ret = ::WSASend(handle_, &buf, 1, &sent, 0, nullptr, nullptr);
    if (ret == SOCKET_ERROR) return std::string("WSASend failed");
    return static_cast<u32>(sent);
}

Result<void> Win32TCPSocket::send_all(const u8* data, u32 len) {
    if (handle_ == INVALID_SOCKET) return std::string("socket not connected");
    u32 total = 0;
    while (total < len) {
        WSABUF buf = { len - total, reinterpret_cast<char*>(const_cast<u8*>(data + total)) };
        DWORD sent = 0;
        int ret = ::WSASend(handle_, &buf, 1, &sent, 0, nullptr, nullptr);
        if (ret == SOCKET_ERROR) return std::string("WSASend failed");
        total += static_cast<u32>(sent);
    }
    return {};
}

Result<u32> Win32TCPSocket::receive(u8* buf, u32 len) {
    if (handle_ == INVALID_SOCKET) return std::string("socket not connected");
    WSABUF wbuf = { len, reinterpret_cast<char*>(buf) };
    DWORD flags = 0;
    DWORD recvd = 0;
    int ret = ::WSARecv(handle_, &wbuf, 1, &recvd, &flags, nullptr, nullptr);
    if (ret == SOCKET_ERROR) return std::string("WSARecv failed");
    return static_cast<u32>(recvd);
}

Result<void> Win32TCPSocket::receive_all(u8* buf, u32 len) {
    if (handle_ == INVALID_SOCKET) return std::string("socket not connected");
    u32 total = 0;
    while (total < len) {
        WSABUF wbuf = { len - total, reinterpret_cast<char*>(buf + total) };
        DWORD flags = 0;
        DWORD recvd = 0;
        int ret = ::WSARecv(handle_, &wbuf, 1, &recvd, &flags, nullptr, nullptr);
        if (ret == SOCKET_ERROR) return std::string("WSARecv failed");
        if (recvd == 0) return std::string("connection closed");
        total += static_cast<u32>(recvd);
    }
    return {};
}

Result<void> Win32TCPSocket::set_read_timeout(u32 ms) {
    if (handle_ == INVALID_SOCKET) return std::string("socket not connected");
    DWORD timeout = ms;
    int ret = ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    if (ret == SOCKET_ERROR) return std::string("setsockopt SO_RCVTIMEO failed");
    return {};
}

void Win32TCPSocket::close() { ensure_closed(); }

bool Win32TCPSocket::is_connected() const { return handle_ != INVALID_SOCKET; }

// --- Async methods ---

async::task<bool> Win32TCPSocket::async_connect(const std::string& host, u16 port) {
    ensure_closed();
    {
        auto iocp_r = ensure_iocp();
        if (iocp_r.is_err()) co_return iocp_r.unwrap_err();
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* result = nullptr;
    auto port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0 || !result)
        co_return std::string("getaddrinfo failed: " + host);

    handle_ = ::WSASocket(result->ai_family, result->ai_socktype, result->ai_protocol,
                          nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == INVALID_SOCKET) { ::freeaddrinfo(result); co_return std::string("WSASocket failed"); }

    auto assoc_r = iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
    if (assoc_r.is_err()) { ::freeaddrinfo(result); ::closesocket(handle_); handle_ = INVALID_SOCKET; co_return std::string("IOCP: ") + assoc_r.unwrap_err(); }

    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    LPFN_CONNECTEX connect_ex = nullptr;
    int wret = WSAIoctl(handle_, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid), &connect_ex, sizeof(connect_ex),
                        &bytes, nullptr, nullptr);
    if (wret == SOCKET_ERROR || !connect_ex) {
        ::freeaddrinfo(result); ::closesocket(handle_); handle_ = INVALID_SOCKET;
        co_return std::string("WSAIoctl ConnectEx failed");
    }

    struct sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = 0;
    if (::bind(handle_, (struct sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        ::freeaddrinfo(result); ::closesocket(handle_); handle_ = INVALID_SOCKET;
        co_return std::string("bind failed");
    }

    IoOverlapped ol;
    BOOL bret = connect_ex(handle_, result->ai_addr, (int)result->ai_addrlen,
                           nullptr, 0, nullptr, &ol);
    ::freeaddrinfo(result);
    if (!bret) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            ::closesocket(handle_); handle_ = INVALID_SOCKET;
            co_return std::string("ConnectEx failed");
        }
        co_await async::iocp_awaiter{&ol};
    }
    if (ol.error) { ::closesocket(handle_); handle_ = INVALID_SOCKET; co_return std::string("ConnectEx error"); }
    co_return true;
}

async::task<bool> Win32TCPSocket::async_connect_ip(const IPv4Address& addr, u16 port) {
    ensure_closed();
    {
        auto iocp_r = ensure_iocp();
        if (iocp_r.is_err()) co_return iocp_r.unwrap_err();
    }

    handle_ = ::WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ == INVALID_SOCKET) co_return std::string("WSASocket failed");

    auto assoc_r = iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
    if (assoc_r.is_err()) { ::closesocket(handle_); handle_ = INVALID_SOCKET; co_return std::string("IOCP: ") + assoc_r.unwrap_err(); }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    u32 ip = (static_cast<u32>(addr.octets[0]) << 24) |
             (static_cast<u32>(addr.octets[1]) << 16) |
             (static_cast<u32>(addr.octets[2]) << 8) |
              static_cast<u32>(addr.octets[3]);
    server_addr.sin_addr.s_addr = htonl(ip);

    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    LPFN_CONNECTEX connect_ex = nullptr;
    int wret = WSAIoctl(handle_, SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid), &connect_ex, sizeof(connect_ex),
                        &bytes, nullptr, nullptr);
    if (wret == SOCKET_ERROR || !connect_ex) {
        ::closesocket(handle_); handle_ = INVALID_SOCKET;
        co_return std::string("WSAIoctl ConnectEx failed");
    }

    struct sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = 0;
    if (::bind(handle_, (struct sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        ::closesocket(handle_); handle_ = INVALID_SOCKET;
        co_return std::string("bind failed");
    }

    IoOverlapped ol;
    BOOL bret = connect_ex(handle_, (struct sockaddr*)&server_addr, sizeof(server_addr),
                           nullptr, 0, nullptr, &ol);
    if (!bret) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            ::closesocket(handle_); handle_ = INVALID_SOCKET;
            co_return std::string("ConnectEx failed");
        }
        co_await async::iocp_awaiter{&ol};
    }
    if (ol.error) { ::closesocket(handle_); handle_ = INVALID_SOCKET; co_return std::string("ConnectEx error"); }
    co_return true;
}

async::task<u32> Win32TCPSocket::async_send(span<u8> data) {
    if (handle_ == INVALID_SOCKET) co_return std::string("not connected");
    WSABUF buf = { data.size(), reinterpret_cast<char*>(data.data()) };
    IoOverlapped ol;
    DWORD sent = 0;
    int ret = ::WSASend(handle_, &buf, 1, &sent, 0, &ol, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) co_return std::string("WSASend failed");
    }
    // Always wait for IOCP completion (even on immediate completion, a packet is queued)
    co_await async::iocp_awaiter{&ol};
    if (ol.error) co_return std::string("WSASend error");
    co_return ol.bytes;
}

async::task<u32> Win32TCPSocket::async_recv(span<u8> buf) {
    if (handle_ == INVALID_SOCKET) co_return std::string("not connected");
    WSABUF wbuf = { buf.size(), reinterpret_cast<char*>(buf.data()) };
    DWORD flags = 0;
    IoOverlapped ol;
    DWORD recvd = 0;
    int ret = ::WSARecv(handle_, &wbuf, 1, &recvd, &flags, &ol, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) co_return std::string("WSARecv failed");
    }
    co_await async::iocp_awaiter{&ol};
    if (ol.error) co_return std::string("WSARecv error");
    co_return ol.bytes;
}

async::task<bool> Win32TCPSocket::async_send_all(span<u8> data) {
    u32 total = 0;
    while (total < data.size()) {
        auto r = co_await async_send(span<u8>(data.data() + total, data.size() - total));
        if (r.is_err()) co_return r.unwrap_err();
        total += r.unwrap();
    }
    co_return true;
}

async::task<bool> Win32TCPSocket::async_recv_exact(span<u8> buf) {
    u32 total = 0;
    while (total < buf.size()) {
        auto r = co_await async_recv(span<u8>(buf.data() + total, buf.size() - total));
        if (r.is_err()) co_return r.unwrap_err();
        u32 n = r.unwrap();
        if (n == 0) co_return std::string("connection closed");
        total += n;
    }
    co_return true;
}

// --- Win32UDPSocket ---

Win32UDPSocket::Win32UDPSocket() : handle_(INVALID_SOCKET), iocp_(nullptr) {
    Win32TCPSocket::ensure_wsa_started();
    handle_ = ::WSASocket(AF_INET, SOCK_DGRAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (handle_ != INVALID_SOCKET) {
        iocp_ = &IOCP::global();
        iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
    }
}

Win32UDPSocket::~Win32UDPSocket() { ensure_closed(); Win32TCPSocket::wsa_cleanup(); }

void Win32UDPSocket::ensure_closed() { if (handle_ != INVALID_SOCKET) { ::closesocket(handle_); handle_ = INVALID_SOCKET; } }

Result<void> Win32UDPSocket::send(const u8* data, u32 len, const IPv4Address& dest, u16 port) {
    if (handle_ == INVALID_SOCKET) return std::string("UDP socket not created");
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    u32 ip = (static_cast<u32>(dest.octets[0]) << 24) |
             (static_cast<u32>(dest.octets[1]) << 16) |
             (static_cast<u32>(dest.octets[2]) << 8) | static_cast<u32>(dest.octets[3]);
    dest_addr.sin_addr.s_addr = htonl(ip);
    WSABUF buf = { len, reinterpret_cast<char*>(const_cast<u8*>(data)) };
    DWORD sent = 0;
    int ret = ::WSASendTo(handle_, &buf, 1, &sent, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr), nullptr, nullptr);
    if (ret == SOCKET_ERROR) return std::string("WSASendTo failed");
    return {};
}

Result<u32> Win32UDPSocket::receive(u8* buf, u32 len, IPv4Address* sender, u16* sender_port) {
    if (handle_ == INVALID_SOCKET) return std::string("UDP socket not created");
    struct sockaddr_in from_addr = {};
    int from_len = sizeof(from_addr);
    WSABUF wbuf = { len, reinterpret_cast<char*>(buf) };
    DWORD flags = 0;
    DWORD recvd = 0;
    int ret = ::WSARecvFrom(handle_, &wbuf, 1, &recvd, &flags, (struct sockaddr*)&from_addr, &from_len, nullptr, nullptr);
    if (ret == SOCKET_ERROR) return std::string("WSARecvFrom failed");
    if (sender) { u32 sip = ntohl(from_addr.sin_addr.s_addr); sender->octets[0] = (u8)(sip>>24); sender->octets[1] = (u8)(sip>>16); sender->octets[2] = (u8)(sip>>8); sender->octets[3] = (u8)sip; }
    if (sender_port) *sender_port = ntohs(from_addr.sin_port);
    return static_cast<u32>(recvd);
}

Result<void> Win32UDPSocket::set_read_timeout(u32 ms) {
    if (handle_ == INVALID_SOCKET) return std::string("UDP socket not created");
    DWORD timeout = ms;
    int ret = ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    if (ret == SOCKET_ERROR) return std::string("setsockopt SO_RCVTIMEO failed");
    return {};
}

void Win32UDPSocket::close() { ensure_closed(); }

async::task<u32> Win32UDPSocket::async_send_to(span<u8> data, const IPv4Address& dest, u16 port) {
    if (handle_ == INVALID_SOCKET) co_return std::string("UDP socket not created");
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    u32 ip = (static_cast<u32>(dest.octets[0]) << 24) |
             (static_cast<u32>(dest.octets[1]) << 16) |
             (static_cast<u32>(dest.octets[2]) << 8) | static_cast<u32>(dest.octets[3]);
    dest_addr.sin_addr.s_addr = htonl(ip);
    WSABUF buf = { data.size(), reinterpret_cast<char*>(data.data()) };
    IoOverlapped ol;
    DWORD sent = 0;
    int ret = ::WSASendTo(handle_, &buf, 1, &sent, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr), &ol, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) co_return std::string("WSASendTo failed");
    }
    co_await async::iocp_awaiter{&ol};
    if (ol.error) co_return std::string("WSASendTo error");
    co_return ol.bytes;
}

async::task<u32> Win32UDPSocket::async_recv_from(span<u8> buf, IPv4Address* sender, u16* sender_port) {
    if (handle_ == INVALID_SOCKET) co_return std::string("UDP socket not created");
    struct sockaddr_in from_addr = {};
    int from_len = sizeof(from_addr);
    WSABUF wbuf = { buf.size(), reinterpret_cast<char*>(buf.data()) };
    DWORD flags = 0;
    IoOverlapped ol;
    DWORD recvd = 0;
    int ret = ::WSARecvFrom(handle_, &wbuf, 1, &recvd, &flags, (struct sockaddr*)&from_addr, &from_len, &ol, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) co_return std::string("WSARecvFrom failed");
    }
    co_await async::iocp_awaiter{&ol};
    if (ol.error) co_return std::string("WSARecvFrom error");
    if (sender) { u32 sip = ntohl(from_addr.sin_addr.s_addr); sender->octets[0] = (u8)(sip>>24); sender->octets[1] = (u8)(sip>>16); sender->octets[2] = (u8)(sip>>8); sender->octets[3] = (u8)sip; }
    if (sender_port) *sender_port = ntohs(from_addr.sin_port);
    co_return ol.bytes;
}

} // namespace browser::net

