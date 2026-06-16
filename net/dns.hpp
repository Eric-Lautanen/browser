#pragma once
#include "socket.hpp"
#include <memory>
#include <vector>
#include <string>

namespace browser::net {

class DNSResolver {
public:
    DNSResolver();
    explicit DNSResolver(const IPv4Address& dns_server);
    ~DNSResolver();
    DNSResolver(DNSResolver&&) noexcept = default;
    DNSResolver& operator=(DNSResolver&&) noexcept = default;
    DNSResolver(const DNSResolver&) = delete;

    Result<std::vector<IPv4Address>> resolve_a(const std::string& hostname);
    void set_dns_server(const IPv4Address& server) { dns_server_ = server; }

    static std::vector<u8> encode_name(const std::string& hostname);

private:
    IPv4Address dns_server_;
    u16 next_tid_ = 1;
    std::unique_ptr<UDPSocket> sock_;

    Result<void> ensure_socket();
    std::vector<u8> build_query(const std::string& hostname, u16 type, u16 id);
    Result<std::vector<IPv4Address>> parse_response(const u8* data, u32 len, u16 expected_id);
};

} // namespace browser::net
