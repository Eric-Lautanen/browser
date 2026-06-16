#pragma once
#include "socket.hpp"
#include "dns.hpp"
#include "../async/task.hpp"
#include <vector>
#include <string>

namespace browser::net {

struct ConnectionConfig {
    u32 connect_timeout_ms = 5000;
    u32 read_timeout_ms = 10000;
    u32 write_timeout_ms = 5000;
};

class Connection {
public:
    Connection();
    ~Connection();
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    Connection(const Connection&) = delete;

    // Sync methods (backward compat)
    Result<void> open(const std::string& host, u16 port, const ConnectionConfig& config = ConnectionConfig{});
    Result<u32> send(const u8* data, u32 len);
    Result<void> send_all(const u8* data, u32 len);
    Result<u32> receive(u8* buf, u32 len);
    Result<std::vector<u8>> receive_until_close(u32 chunk_size = 4096);
    void close();
    bool is_open() const;
    Socket* socket() { return socket_.get(); }
    const std::string& host() const { return host_; }
    u16 port() const { return port_; }

    // Async methods (bool return = true on success, error on failure)
    async::task<bool> open_async(const std::string& host, u16 port, const ConnectionConfig& config = ConnectionConfig{});
    async::task<u32> send_async(const u8* data, u32 len);
    async::task<bool> send_all_async(const u8* data, u32 len);
    async::task<u32> receive_async(u8* buf, u32 len);
    async::task<std::vector<u8>> receive_until_close_async(u32 chunk_size = 4096);

private:
    std::unique_ptr<Socket> socket_;
    DNSResolver resolver_;
    std::string host_;
    u16 port_ = 0;
};

} // namespace browser::net
