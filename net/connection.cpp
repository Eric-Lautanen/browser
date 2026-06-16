#include "connection.hpp"

namespace browser::net {

Connection::Connection() = default;

Connection::~Connection() { close(); }

Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

Result<void> Connection::open(const std::string& host, u16 port, const ConnectionConfig& config) {
    host_ = host;
    port_ = port;

    std::vector<IPv4Address> ips;
    auto parsed = IPv4Address::from_string(host);
    if (parsed.is_ok()) {
        ips.push_back(parsed.unwrap());
    } else if (host == "localhost") {
        ips.push_back(IPv4Address(127, 0, 0, 1));
    } else {
        // Sync resolve - use the task and sync_wait
        auto addrs_task = resolver_.resolve_a(host);
        auto addrs_result = addrs_task.sync_wait();
        if (addrs_result.is_err()) {
            return std::string("DNS resolution failed for " + host + ": " + addrs_result.unwrap_err());
        }
        ips = addrs_result.unwrap();
    }

    if (ips.empty()) return std::string("no addresses resolved for " + host);

    auto sock = Socket::create_tcp();
    if (sock.is_err()) return std::string("failed to create TCP socket");
    socket_ = std::move(sock.unwrap());

    for (auto& ip : ips) {
        auto r = socket_->connect_ip(ip, port);
        if (r.is_ok()) {
            auto to = socket_->set_read_timeout(config.read_timeout_ms);
            if (to.is_err()) { socket_->close(); return std::string("failed to set read timeout"); }
            return {};
        }
    }

    socket_.reset();
    return std::string("failed to connect to " + host);
}

Result<u32> Connection::send(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) return std::string("connection not open");
    return socket_->send(data, len);
}

Result<void> Connection::send_all(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) return std::string("connection not open");
    return socket_->send_all(data, len);
}

Result<u32> Connection::receive(u8* buf, u32 len) {
    if (!socket_ || !socket_->is_connected()) return std::string("connection not open");
    return socket_->receive(buf, len);
}

Result<std::vector<u8>> Connection::receive_until_close(u32 chunk_size) {
    if (!socket_ || !socket_->is_connected()) return std::string("connection not open");
    std::vector<u8> result;
    std::vector<u8> buf(chunk_size);
    while (true) {
        auto r = socket_->receive(buf.data(), chunk_size);
        if (r.is_err()) {
            if (result.empty()) return std::string("receive failed: " + r.unwrap_err());
            break;
        }
        u32 n = r.unwrap();
        if (n == 0) break;
        result.insert(result.end(), buf.data(), buf.data() + n);
    }
    return result;
}

void Connection::close() {
    if (socket_) { socket_->close(); socket_.reset(); }
}

bool Connection::is_open() const {
    return socket_ && socket_->is_connected();
}

// --- Async methods ---

async::task<bool> Connection::open_async(const std::string& host, u16 port, const ConnectionConfig& config) {
    host_ = host;
    port_ = port;

    std::vector<IPv4Address> ips;
    auto parsed = IPv4Address::from_string(host);
    if (parsed.is_ok()) {
        ips.push_back(parsed.unwrap());
    } else if (host == "localhost") {
        ips.push_back(IPv4Address(127, 0, 0, 1));
    } else {
        auto addrs_task = resolver_.resolve_a(host);
        auto addrs_r = co_await addrs_task;
        if (addrs_r.is_err()) {
            co_return std::string("DNS failed for " + host + ": ") + addrs_r.unwrap_err();
        }
        ips = addrs_r.unwrap();
    }

    if (ips.empty()) co_return std::string("no addresses for ") + host;

    auto sock = Socket::create_tcp();
    if (sock.is_err()) co_return std::string("failed to create TCP socket");
    socket_ = std::move(sock.unwrap());

    for (auto& ip : ips) {
        auto conn_task = socket_->async_connect_ip(ip, port);
        auto r = co_await conn_task;
        if (r.is_ok()) {
            auto to = socket_->set_read_timeout(config.read_timeout_ms);
            if (to.is_err()) { socket_->close(); co_return std::string("set read timeout failed"); }
            co_return true;
        }
    }

    socket_.reset();
    co_return std::string("failed to connect to ") + host;
}

async::task<u32> Connection::send_async(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) co_return std::string("not open");
    auto r = co_await socket_->async_send(span<u8>(const_cast<u8*>(data), len));
    co_return r;
}

async::task<bool> Connection::send_all_async(const u8* data, u32 len) {
    if (!socket_ || !socket_->is_connected()) co_return std::string("not open");
    auto r = co_await socket_->async_send_all(span<u8>(const_cast<u8*>(data), len));
    co_return r;
}

async::task<u32> Connection::receive_async(u8* buf, u32 len) {
    if (!socket_ || !socket_->is_connected()) co_return std::string("not open");
    auto r = co_await socket_->async_recv(span<u8>(buf, len));
    co_return r;
}

async::task<std::vector<u8>> Connection::receive_until_close_async(u32 chunk_size) {
    if (!socket_ || !socket_->is_connected()) co_return std::string("not open");
    std::vector<u8> result;
    std::vector<u8> buf(chunk_size);
    while (true) {
        auto r = co_await socket_->async_recv(span<u8>(buf.data(), chunk_size));
        if (r.is_err()) {
            if (result.empty()) co_return std::string("receive: ") + r.unwrap_err();
            break;
        }
        u32 n = r.unwrap();
        if (n == 0) break;
        result.insert(result.end(), buf.data(), buf.data() + n);
    }
    co_return result;
}

} // namespace browser::net

