#pragma once
#include "../async/task.hpp"
#include "dns.hpp"
#include "socket.hpp"

#include <string>
#include <vector>

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
        Connection(Connection &&) noexcept;
        Connection &operator=(Connection &&) noexcept;
        Connection(const Connection &) = delete;

        // Sync methods (backward compat)
        Result<void> open(const std::string &host, u16 port, const ConnectionConfig &config = ConnectionConfig{});
        Result<u32> send(const u8 *data, u32 len);
        Result<void> send_all(const u8 *data, u32 len);
        Result<u32> receive(u8 *buf, u32 len);
        Result<std::vector<u8>> receive_until_close(u32 chunk_size = 4096);
        void close();
        bool is_open() const;
        Socket *socket() { return socket_.get(); }
        const std::string &host() const { return host_; }
        u16 port() const { return port_; }

        // Async methods (bool return = true on success, error on failure)
        async::task<bool> open_async(const std::string &host,
                                     u16 port,
                                     const ConnectionConfig &config = ConnectionConfig{});
        async::task<u32> send_async(const u8 *data, u32 len);
        async::task<bool> send_all_async(const u8 *data, u32 len);
        async::task<u32> receive_async(u8 *buf, u32 len);
        async::task<std::vector<u8>> receive_until_close_async(u32 chunk_size = 4096);

    private:
        std::unique_ptr<Socket> socket_;
        DNSResolver resolver_;
        std::string host_;
        u16 port_ = 0;
    };

    // Owns or borrows a Connection. Sub-protocol clients (HTTP/1.1, HTTP/2) borrow
    // the caller's socket when adopting an already-established connection, and own
    // one when connecting independently. Borrowed sockets are never destroyed here.
    class ConnectionRef {
    public:
        ConnectionRef() = default;

        void adopt(Connection &c) {
            owned_.reset();
            borrowed_ = &c;
        }

        Connection &obtain() {
            borrowed_ = nullptr;
            if (!owned_)
                owned_ = std::make_unique<Connection>();
            return *owned_;
        }

        Connection &get() { return borrowed_ ? *borrowed_ : *owned_; }
        const Connection &get() const { return borrowed_ ? *borrowed_ : *owned_; }
        Connection *operator->() { return &get(); }
        const Connection *operator->() const { return &get(); }
        Connection &operator*() { return get(); }
        const Connection &operator*() const { return get(); }

        bool valid() const { return borrowed_ != nullptr || owned_ != nullptr; }

        // Closes the underlying socket if one is held or borrowed; no-op otherwise.
        void close() {
            if (borrowed_)
                borrowed_->close();
            else if (owned_)
                owned_->close();
        }

    private:
        Connection *borrowed_ = nullptr;
        std::unique_ptr<Connection> owned_;
    };

}  // namespace browser::net
