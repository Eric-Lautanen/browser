#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "socket.hpp"
#include "connection.hpp"
#include "tls.hpp"
#include "url.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace browser::net::ws {

enum class Opcode : u8 {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

enum class CloseCode : u16 {
    NORMAL = 1000,
    GOING_AWAY = 1001,
    PROTOCOL_ERROR = 1002,
    UNSUPPORTED = 1003,
    ABNORMAL = 1006,
    POLICY_VIOLATION = 1008,
    TOO_LARGE = 1009,
    EXTENSION_MISSING = 1010,
    INTERNAL_ERROR = 1011
};

struct WebSocketFrame {
    bool fin;
    Opcode opcode;
    bool masked;
    u8 mask_key[4];
    u64 payload_length;
    std::vector<u8> payload;
};

class WebSocket {
public:
    WebSocket();
    ~WebSocket();

    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    WebSocket(WebSocket&&) noexcept;
    WebSocket& operator=(WebSocket&&) noexcept;

    // Connect to a WebSocket server
    async::task<bool> connect(const std::string& url_str);
    async::task<bool> connect(const std::string& host, u16 port, const std::string& path, bool tls);

    // Send methods
    async::task<bool> send_text(const std::string& text);
    async::task<bool> send_binary(span<u8> data);
    async::task<bool> send_frame(const WebSocketFrame& frame);
    async::task<bool> send_ping();
    async::task<bool> send_pong();
    async::task<bool> send_close(CloseCode code = CloseCode::NORMAL, const std::string& reason = "");

    // Receive methods
    async::task<WebSocketFrame> receive_frame();
    async::task<std::vector<u8>> receive_text();

    // Control
    async::task<bool> close(CloseCode code = CloseCode::NORMAL, const std::string& reason = "");
    void close_sync();
    bool is_connected() const;

    // Callbacks
    void on_message(std::function<void(const std::string&)> cb) { on_message_cb_ = std::move(cb); }
    void on_close(std::function<void(CloseCode, const std::string&)> cb) { on_close_cb_ = std::move(cb); }
    void on_error(std::function<void(const std::string&)> cb) { on_error_cb_ = std::move(cb); }

    static std::vector<u8> encode_frame(const WebSocketFrame& frame);
    static Result<WebSocketFrame> decode_frame(const u8* data, u32 len, u32& consumed);

private:
    std::unique_ptr<Connection> tcp_;
    std::unique_ptr<tls::TLSConnection> tls_;
    bool use_tls_ = false;
    bool connected_ = false;
    bool closing_ = false;
    std::vector<u8> recv_buffer_;

    std::function<void(const std::string&)> on_message_cb_;
    std::function<void(CloseCode, const std::string&)> on_close_cb_;
    std::function<void(const std::string&)> on_error_cb_;

    async::task<bool> perform_handshake(const std::string& host, u16 port, const std::string& path);
    std::string generate_key() const;
    static std::string compute_accept_key(const std::string& key);

    // I/O helpers
    async::task<bool> send_raw(span<u8> data);
    async::task<u32> recv_raw(u8* buf, u32 len);
};

} // namespace browser::net::ws

