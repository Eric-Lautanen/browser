#include "dns.hpp"
#include "../async/executor.hpp"
#include <cstring>
#include <vector>
#include <string>
#include <memory>

namespace browser::net {

DNSResolver::DNSResolver()
    : dns_server_(8, 8, 8, 8) {}

DNSResolver::DNSResolver(const IPv4Address& dns_server)
    : dns_server_(dns_server) {}

DNSResolver::~DNSResolver() = default;

Result<void> DNSResolver::ensure_socket() {
    if (sock_) return {};
    auto r = UDPSocket::create();
    if (r.is_err()) return std::string("failed to create UDP socket: " + r.unwrap_err());
    sock_ = std::move(r.unwrap());
    auto timeout_r = sock_->set_read_timeout(5000);
    if (timeout_r.is_err()) {
        sock_.reset();
        return std::string("failed to set UDP timeout");
    }
    return {};
}

std::vector<u8> DNSResolver::encode_name(const std::string& hostname) {
    std::vector<u8> result;
    std::size_t start = 0;
    while (start < hostname.size()) {
        auto dot = hostname.find('.', start);
        if (dot == std::string::npos) dot = hostname.size();
        auto label_len = dot - start;
        result.push_back(static_cast<u8>(label_len));
        for (std::size_t i = start; i < dot; i++) {
            result.push_back(static_cast<u8>(hostname[i]));
        }
        start = dot + 1;
    }
    result.push_back(0);
    return result;
}

std::vector<u8> DNSResolver::build_query(const std::string& hostname, u16 type, u16 id) {
    std::vector<u8> buf;

    auto add_u16 = [&](u16 v) {
        buf.push_back(static_cast<u8>(v >> 8));
        buf.push_back(static_cast<u8>(v & 0xFF));
    };

    add_u16(id);
    add_u16(0x0100);
    add_u16(1);
    add_u16(0);
    add_u16(0);
    add_u16(0);

    auto encoded = encode_name(hostname);
    buf.insert(buf.end(), encoded.begin(), encoded.end());

    add_u16(type);
    add_u16(1);

    return buf;
}

static Result<std::pair<std::string, u16>> read_dns_name(const u8* data, u32 len, u32 offset) {
    std::string name;
    u32 pos = offset;
    u16 consumed = 0;
    bool jumped = false;
    const int max_labels = 128;
    int labels = 0;

    while (pos < len) {
        if (labels++ > max_labels) return std::string("DNS name too many labels");
        u8 b = data[pos];
        if (b == 0) {
            if (!jumped) consumed = static_cast<u16>(pos - offset + 1);
            pos++;
            break;
        }
        if ((b & 0xC0) == 0xC0) {
            if (pos + 1 >= len) return std::string("truncated DNS pointer");
            u16 ptr = ((static_cast<u16>(b & 0x3F)) << 8) | static_cast<u16>(data[pos + 1]);
            if (ptr >= len) return std::string("DNS pointer out of bounds");
            if (!jumped) { consumed = static_cast<u16>(pos - offset + 2); jumped = true; }
            pos = ptr;
            continue;
        }
        u8 label_len = b;
        if (pos + 1 + label_len > len) return std::string("truncated DNS label");
        if (!name.empty()) name += '.';
        for (u8 i = 0; i < label_len; i++) name += static_cast<char>(data[pos + 1 + i]);
        pos += 1 + label_len;
    }
    return std::make_pair(name, consumed);
}

async::task<std::vector<IPv4Address>> DNSResolver::resolve_a(const std::string& hostname) {
    u16 id = next_tid_++;
    auto query = build_query(hostname, 1, id);

    auto sr = ensure_socket();
    if (sr.is_err()) co_return std::string("socket: ") + sr.unwrap_err();

    auto send_r = co_await sock_->async_send_to(span<u8>(query.data(), static_cast<u32>(query.size())), dns_server_, 53);
    if (send_r.is_err()) co_return std::string("DNS send: ") + send_r.unwrap_err();

    u8 recv_buf[1500];
    IPv4Address sender;
    u16 sender_port;
    auto recv_r = co_await sock_->async_recv_from(span<u8>(recv_buf, sizeof(recv_buf)), &sender, &sender_port);
    if (recv_r.is_err()) co_return std::string("DNS recv: ") + recv_r.unwrap_err();

    u32 recv_len = recv_r.unwrap();
    co_return parse_response(recv_buf, recv_len, id);
}

Result<std::vector<IPv4Address>> DNSResolver::parse_response(const u8* data, u32 len, u16 expected_id) {
    std::vector<IPv4Address> result;
    if (len < 12) return std::string("DNS response too short");

    u16 id = (static_cast<u16>(data[0]) << 8) | data[1];
    if (id != expected_id) return std::string("DNS response ID mismatch");

    u16 flags = (static_cast<u16>(data[2]) << 8) | data[3];
    if ((flags & 0x8000) == 0) return std::string("DNS response is not a response");
    u8 rcode = flags & 0x0F;
    if (rcode != 0) return std::string("DNS response error code: ") + std::to_string(rcode);

    u16 qdcount = (static_cast<u16>(data[4]) << 8) | data[5];
    u16 ancount = (static_cast<u16>(data[6]) << 8) | data[7];
    u32 pos = 12;

    for (u16 i = 0; i < qdcount; i++) {
        auto name_r = read_dns_name(data, len, pos);
        if (name_r.is_err()) return name_r.unwrap_err();
        pos += name_r.unwrap().second;
        if (pos + 4 > len) return std::string("truncated DNS question");
        pos += 4;
    }

    for (u16 i = 0; i < ancount; i++) {
        if (pos >= len) return std::string("truncated DNS answer section");
        auto name_r = read_dns_name(data, len, pos);
        if (name_r.is_err()) return name_r.unwrap_err();
        pos += name_r.unwrap().second;
        if (pos + 10 > len) return std::string("truncated DNS RR header");

        u16 rtype = (static_cast<u16>(data[pos]) << 8) | data[pos + 1];
        u16 rclass = (static_cast<u16>(data[pos + 2]) << 8) | data[pos + 3];
        (void)rclass;
        u32 rttl = (static_cast<u32>(data[pos + 4]) << 24) | (static_cast<u32>(data[pos + 5]) << 16) |
                   (static_cast<u32>(data[pos + 6]) << 8) | static_cast<u32>(data[pos + 7]);
        (void)rttl;
        u16 rdlength = (static_cast<u16>(data[pos + 8]) << 8) | data[pos + 9];
        pos += 10;
        if (pos + rdlength > len) return std::string("truncated DNS RDATA");
        if (rtype == 1 && rdlength == 4) {
            IPv4Address addr;
            addr.octets[0] = data[pos];
            addr.octets[1] = data[pos + 1];
            addr.octets[2] = data[pos + 2];
            addr.octets[3] = data[pos + 3];
            result.push_back(addr);
        }
        pos += rdlength;
    }
    return result;
}

} // namespace browser::net
