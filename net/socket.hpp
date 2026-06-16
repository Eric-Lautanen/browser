#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "../tests/utility.hpp"

namespace browser::net {

struct IPv4Address {
    u8 octets[4];

    IPv4Address() : octets{0, 0, 0, 0} {}
    IPv4Address(u8 a, u8 b, u8 c, u8 d) : octets{a, b, c, d} {}

    std::string to_string() const;
    static Result<IPv4Address> from_string(const std::string& s);
    bool operator==(const IPv4Address& other) const;
    bool operator!=(const IPv4Address& other) const { return !(*this == other); }
};

struct IPv6Address {
    u16 groups[8];

    IPv6Address();
    std::string to_string() const;
    static Result<IPv6Address> from_string(const std::string& s);
    bool operator==(const IPv6Address& other) const;
};

class Socket {
public:
    Socket() = default;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) noexcept = default;
    Socket& operator=(Socket&&) noexcept = default;
    virtual ~Socket() = default;

    virtual Result<void> connect(const std::string& host, u16 port) = 0;
    virtual Result<void> connect_ip(const IPv4Address& addr, u16 port) = 0;
    virtual Result<u32> send(const u8* data, u32 len) = 0;
    virtual Result<void> send_all(const u8* data, u32 len) = 0;
    virtual Result<u32> receive(u8* buf, u32 len) = 0;
    virtual Result<void> receive_all(u8* buf, u32 len) = 0;
    virtual Result<void> set_read_timeout(u32 ms) = 0;
    virtual void close() = 0;
    virtual bool is_connected() const = 0;

    static Result<std::unique_ptr<Socket>> create_tcp();
};

class UDPSocket {
public:
    UDPSocket() = default;
    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;
    UDPSocket(UDPSocket&&) noexcept = default;
    UDPSocket& operator=(UDPSocket&&) noexcept = default;
    virtual ~UDPSocket() = default;

    virtual Result<void> send(const u8* data, u32 len, const IPv4Address& dest, u16 port) = 0;
    virtual Result<u32> receive(u8* buf, u32 len, IPv4Address* sender, u16* sender_port) = 0;
    virtual Result<void> set_read_timeout(u32 ms) = 0;
    virtual void close() = 0;

    static Result<std::unique_ptr<UDPSocket>> create();
};

} // namespace browser::net
