#include "connection.hpp"

namespace browser::net {

Connection::Connection() = default;

Connection::~Connection() {
    close();
}

Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

Result<void> Connection::open(const std::string& host, u16 port, const ConnectionConfig& config) {
    host_ = host;
    port_ = port;

    // Bypass DNS for known IP addresses and localhost
    std::vector<IPv4Address> ips;
    auto parsed = IPv4Address::from_string(host);
    if (parsed.is_ok()) {
        ips.push_back(parsed.unwrap());
    } else if (host == "localhost") {
        ips.push_back(IPv4Address(127, 0, 0, 1));
    } else {
        auto addrs = resolver_.resolve_a(host);
        if (addrs.is_err()) {
            return std::string("DNS resolution failed for " + host + ": " + addrs.unwrap_err());
        }
        ips = addrs.unwrap();
    }

    if (ips.empty()) {
        return std::string("no addresses resolved for " + host);
    }

    auto sock = Socket::create_tcp();
    if (sock.is_err()) {
        return std::string("failed to create TCP socket");
    }
    socket_ = std::move(sock.unwrap());

    for (auto& ip : ips) {
        auto r = socket_->connect_ip(ip, port);
        if (r.is_ok()) {
            auto to = socket_->set_read_timeout(config.read_timeout_ms);
            if (to.is_err()) {
                socket_->close();
                return std::string("failed to set read timeout");
            }
            return {};
        }
    }

    socket_.reset();
    return std::string("failed to connect to " + host);
}

Result<u32> Connection::send(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) {
        return std::string("connection not open");
    }
    return socket_->send(data, len);
}

Result<void> Connection::send_all(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) {
        return std::string("connection not open");
    }
    return socket_->send_all(data, len);
}

Result<u32> Connection::receive(u8* buf, u32 len) {
    if (!socket_ || !socket_->is_connected()) {
        return std::string("connection not open");
    }
    return socket_->receive(buf, len);
}

Result<std::vector<u8>> Connection::receive_until_close(u32 chunk_size) {
    if (!socket_ || !socket_->is_connected()) {
        return std::string("connection not open");
    }
    std::vector<u8> result;
    std::vector<u8> buf(chunk_size);
    while (true) {
        auto r = socket_->receive(buf.data(), chunk_size);
        if (r.is_err()) {
            if (result.empty()) {
                return std::string("receive failed: " + r.unwrap_err());
            }
            break;
        }
        u32 n = r.unwrap();
        if (n == 0) break;
        result.insert(result.end(), buf.data(), buf.data() + n);
    }
    return result;
}

void Connection::close() {
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
}

bool Connection::is_open() const {
    return socket_ && socket_->is_connected();
}

} // namespace browser::net
