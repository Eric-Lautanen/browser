#pragma once
#include "socket.hpp"
#include "iocp.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <coroutine>

namespace browser::net {

class Win32TCPSocket : public Socket {
public:
    Win32TCPSocket();
    ~Win32TCPSocket() override;

    Result<void> connect(const std::string& host, u16 port) override;
    Result<void> connect_ip(const IPv4Address& addr, u16 port) override;
    Result<u32> send(const u8* data, u32 len) override;
    Result<void> send_all(const u8* data, u32 len) override;
    Result<u32> receive(u8* buf, u32 len) override;
    Result<void> receive_all(u8* buf, u32 len) override;
    Result<void> set_read_timeout(u32 ms) override;
    void close() override;
    bool is_connected() const override;

    async::task<void> async_connect(const std::string& host, u16 port) override;
    async::task<void> async_connect_ip(const IPv4Address& addr, u16 port) override;
    async::task<u32> async_send(span<u8> data) override;
    async::task<u32> async_recv(span<u8> buf) override;
    async::task<void> async_send_all(span<u8> data) override;
    async::task<void> async_recv_exact(span<u8> buf) override;

public:
    static Result<void> ensure_wsa_started();
    static void wsa_cleanup();

private:
    SOCKET handle_;
    IOCP* iocp_;
    LPFN_CONNECTEX connect_ex_func_;

    void ensure_closed();
    Result<void> ensure_iocp();
    Result<void> load_connect_ex();

    static int wsa_refcount_;
    static bool iocp_initialized_;
};

class Win32UDPSocket : public UDPSocket {
public:
    Win32UDPSocket();
    ~Win32UDPSocket() override;

    Result<void> send(const u8* data, u32 len, const IPv4Address& dest, u16 port) override;
    Result<u32> receive(u8* buf, u32 len, IPv4Address* sender, u16* sender_port) override;
    Result<void> set_read_timeout(u32 ms) override;
    void close() override;

    async::task<u32> async_send_to(span<u8> data, const IPv4Address& dest, u16 port) override;
    async::task<u32> async_recv_from(span<u8> buf, IPv4Address* sender = nullptr, u16* sender_port = nullptr) override;

private:
    SOCKET handle_;
    IOCP* iocp_;
    void ensure_closed();
};

} // namespace browser::net
