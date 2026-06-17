#include "socket_win32.hpp"
#include "../../async/executor.hpp"

namespace browser::net {

    Win32UDPSocket::Win32UDPSocket() : handle_(INVALID_SOCKET), iocp_(nullptr) {
        Win32TCPSocket::ensure_wsa_started();
        handle_ = ::WSASocket(AF_INET, SOCK_DGRAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (handle_ != INVALID_SOCKET) {
            iocp_ = &IOCP::global();
            iocp_->associate_socket(handle_, reinterpret_cast<ULONG_PTR>(this));
        }
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

    Result<void> Win32UDPSocket::send(const u8 *data, u32 len, const IPv4Address &dest, u16 port) {
        if (handle_ == INVALID_SOCKET)
            return std::string("UDP socket not created");
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        u32 ip = (static_cast<u32>(dest.octets[0]) << 24) | (static_cast<u32>(dest.octets[1]) << 16) |
                 (static_cast<u32>(dest.octets[2]) << 8) | static_cast<u32>(dest.octets[3]);
        dest_addr.sin_addr.s_addr = htonl(ip);
        WSABUF buf = {len, reinterpret_cast<char *>(const_cast<u8 *>(data))};
        DWORD sent = 0;
        int ret =
            ::WSASendTo(handle_, &buf, 1, &sent, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr), nullptr, nullptr);
        if (ret == SOCKET_ERROR)
            return std::string("WSASendTo failed");
        return {};
    }

    Result<u32> Win32UDPSocket::receive(u8 *buf, u32 len, IPv4Address *sender, u16 *sender_port) {
        if (handle_ == INVALID_SOCKET)
            return std::string("UDP socket not created");
        struct sockaddr_in from_addr = {};
        int from_len = sizeof(from_addr);
        WSABUF wbuf = {len, reinterpret_cast<char *>(buf)};
        DWORD flags = 0;
        DWORD recvd = 0;
        int ret = ::WSARecvFrom(
            handle_, &wbuf, 1, &recvd, &flags, (struct sockaddr *)&from_addr, &from_len, nullptr, nullptr);
        if (ret == SOCKET_ERROR)
            return std::string("WSARecvFrom failed");
        if (sender) {
            u32 sip = ntohl(from_addr.sin_addr.s_addr);
            sender->octets[0] = (u8)(sip >> 24);
            sender->octets[1] = (u8)(sip >> 16);
            sender->octets[2] = (u8)(sip >> 8);
            sender->octets[3] = (u8)sip;
        }
        if (sender_port)
            *sender_port = ntohs(from_addr.sin_port);
        return static_cast<u32>(recvd);
    }

    Result<void> Win32UDPSocket::set_read_timeout(u32 ms) {
        if (handle_ == INVALID_SOCKET)
            return std::string("UDP socket not created");
        DWORD timeout = ms;
        int ret = ::setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        if (ret == SOCKET_ERROR)
            return std::string("setsockopt SO_RCVTIMEO failed");
        return {};
    }

    void Win32UDPSocket::close() {
        ensure_closed();
    }

    async::task<u32> Win32UDPSocket::async_send_to(span<u8> data, const IPv4Address &dest, u16 port) {
        if (handle_ == INVALID_SOCKET)
            co_return std::string("UDP socket not created");
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);
        u32 ip = (static_cast<u32>(dest.octets[0]) << 24) | (static_cast<u32>(dest.octets[1]) << 16) |
                 (static_cast<u32>(dest.octets[2]) << 8) | static_cast<u32>(dest.octets[3]);
        dest_addr.sin_addr.s_addr = htonl(ip);
        WSABUF buf = {data.size(), reinterpret_cast<char *>(data.data())};
        IoOverlapped ol;
        DWORD sent = 0;
        int ret =
            ::WSASendTo(handle_, &buf, 1, &sent, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr), &ol, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
                co_return std::string("WSASendTo failed");
        }
        co_await async::iocp_awaiter{&ol};
        if (ol.error)
            co_return std::string("WSASendTo error");
        co_return ol.bytes;
    }

    async::task<u32> Win32UDPSocket::async_recv_from(span<u8> buf, IPv4Address *sender, u16 *sender_port) {
        if (handle_ == INVALID_SOCKET)
            co_return std::string("UDP socket not created");
        struct sockaddr_in from_addr = {};
        int from_len = sizeof(from_addr);
        WSABUF wbuf = {buf.size(), reinterpret_cast<char *>(buf.data())};
        DWORD flags = 0;
        IoOverlapped ol;
        DWORD recvd = 0;
        int ret =
            ::WSARecvFrom(handle_, &wbuf, 1, &recvd, &flags, (struct sockaddr *)&from_addr, &from_len, &ol, nullptr);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING)
                co_return std::string("WSARecvFrom failed");
        }
        co_await async::iocp_awaiter{&ol};
        if (ol.error)
            co_return std::string("WSARecvFrom error");
        if (sender) {
            u32 sip = ntohl(from_addr.sin_addr.s_addr);
            sender->octets[0] = (u8)(sip >> 24);
            sender->octets[1] = (u8)(sip >> 16);
            sender->octets[2] = (u8)(sip >> 8);
            sender->octets[3] = (u8)sip;
        }
        if (sender_port)
            *sender_port = ntohs(from_addr.sin_port);
        co_return ol.bytes;
    }

}  // namespace browser::net
