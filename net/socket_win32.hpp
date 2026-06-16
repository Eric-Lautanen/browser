#pragma once
#include "socket.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>

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

public:
    static Result<void> ensure_wsa_started();
    static void wsa_cleanup();

private:
    SOCKET handle_;
    void ensure_closed();

    static int wsa_refcount_;
};

class Win32UDPSocket : public UDPSocket {
public:
    Win32UDPSocket();
    ~Win32UDPSocket() override;

    Result<void> send(const u8* data, u32 len, const IPv4Address& dest, u16 port) override;
    Result<u32> receive(u8* buf, u32 len, IPv4Address* sender, u16* sender_port) override;
    Result<void> set_read_timeout(u32 ms) override;
    void close() override;

private:
    SOCKET handle_;
    void ensure_closed();
};

} // namespace browser::net
