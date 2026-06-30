#include "websocket.hpp"
#include "../async/executor.hpp"
#include <cstring>
#include <sstream>
#include <random>
#include <algorithm>

namespace browser::net::ws {

namespace sha1 {
    struct SHA1 {
        u32 state[5];
        u64 count;
        u8 buffer[64];

        SHA1() { reset(); }

        void reset() {
            state[0] = 0x67452301;
            state[1] = 0xEFCDAB89;
            state[2] = 0x98BADCFE;
            state[3] = 0x10325476;
            state[4] = 0xC3D2E1F0;
            count = 0;
        }

        void update(const u8* data, u32 len) {
            u32 idx = static_cast<u32>(count & 63);
            count += len;
            u32 part = 64 - idx;
            if (len >= part) {
                std::memcpy(buffer + idx, data, part);
                transform(buffer);
                for (u32 i = part; i + 63 < len; i += 64)
                    transform(data + i);
                idx = 0;
            } else {
                part = len;
            }
            std::memcpy(buffer + idx, data + (len - part), part);
        }

        std::vector<u8> digest() {
            u64 bit_count = count << 3;
            u32 idx = static_cast<u32>(count & 63);
            u8 bits[8];
            for (int i = 0; i < 8; i++)
                bits[i] = static_cast<u8>((bit_count >> (56 - i * 8)) & 0xFF);
            buffer[idx] = 0x80;
            if (idx >= 56) {
                std::memset(buffer + idx + 1, 0, 64 - idx - 1);
                transform(buffer);
                std::memset(buffer, 0, 56);
            } else {
                std::memset(buffer + idx + 1, 0, 56 - idx - 1);
            }
            std::memcpy(buffer + 56, bits, 8);
            transform(buffer);
            std::vector<u8> result(20);
            for (int i = 0; i < 5; i++) {
                result[i * 4 + 0] = static_cast<u8>((state[i] >> 24) & 0xFF);
                result[i * 4 + 1] = static_cast<u8>((state[i] >> 16) & 0xFF);
                result[i * 4 + 2] = static_cast<u8>((state[i] >> 8) & 0xFF);
                result[i * 4 + 3] = static_cast<u8>(state[i] & 0xFF);
            }
            return result;
        }

    private:
        static u32 rotl(u32 x, u32 n) { return (x << n) | (x >> (32 - n)); }

        void transform(const u8* block) {
            u32 w[80];
            for (int i = 0; i < 16; i++)
                w[i] = (static_cast<u32>(block[i * 4]) << 24) |
                       (static_cast<u32>(block[i * 4 + 1]) << 16) |
                       (static_cast<u32>(block[i * 4 + 2]) << 8) |
                       static_cast<u32>(block[i * 4 + 3]);
            for (int i = 16; i < 80; i++)
                w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

            u32 a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
            for (int i = 0; i < 80; i++) {
                u32 f, k;
                if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else { f = b ^ c ^ d; k = 0xCA62C1D6; }
                u32 tmp = rotl(a, 5) + f + e + k + w[i];
                e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
            }
            state[0] += a; state[1] += b; state[2] += c;
            state[3] += d; state[4] += e;
        }
    };

    static std::vector<u8> hash(const u8* data, u32 len) {
        SHA1 ctx;
        ctx.update(data, len);
        return ctx.digest();
    }
}

WebSocket::WebSocket() = default;
WebSocket::~WebSocket() { close_sync(); }
WebSocket::WebSocket(WebSocket&&) noexcept = default;
WebSocket& WebSocket::operator=(WebSocket&&) noexcept = default;

bool WebSocket::is_connected() const { return connected_; }

void WebSocket::close_sync() {
    connected_ = false;
    closing_ = false;
    if (tls_) { tls_->close(); tls_.reset(); }
    if (tcp_) { tcp_->close(); tcp_.reset(); }
}

std::string WebSocket::generate_key() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    u8 key[16];
    for (int i = 0; i < 16; i++) key[i] = static_cast<u8>(dis(gen));
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (int i = 0; i < 16; i += 3) {
        int remaining = 16 - i;
        u32 v = (static_cast<u32>(key[i]) << 16);
        if (remaining > 1) v |= (static_cast<u32>(key[i + 1]) << 8);
        if (remaining > 2) v |= static_cast<u32>(key[i + 2]);
        result += b64[(v >> 18) & 0x3F];
        result += b64[(v >> 12) & 0x3F];
        if (remaining > 1) result += b64[(v >> 6) & 0x3F];
        else result += '=';
        if (remaining > 2) result += b64[v & 0x3F];
        else result += '=';
    }
    // 16 bytes = 24 base64 chars, no extra padding needed
    return result;
}

std::string WebSocket::compute_accept_key(const std::string& key) {
    std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    auto hash = sha1::hash(reinterpret_cast<const u8*>(magic.data()), static_cast<u32>(magic.size()));
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < hash.size(); i += 3) {
        int remaining = static_cast<int>(hash.size() - i);
        u32 v = (static_cast<u32>(hash[i]) << 16);
        if (remaining > 1) v |= (static_cast<u32>(hash[i + 1]) << 8);
        if (remaining > 2) v |= static_cast<u32>(hash[i + 2]);
        result += b64[(v >> 18) & 0x3F];
        result += b64[(v >> 12) & 0x3F];
        if (remaining > 1) result += b64[(v >> 6) & 0x3F];
        else result += '=';
        if (remaining > 2) result += b64[v & 0x3F];
        else result += '=';
    }
    return result;
}

async::task<bool> WebSocket::connect(const std::string& url_str) {
    auto url_r = URL::parse(url_str);
    if (url_r.is_err()) co_return std::string("bad URL: ") + url_r.unwrap_err();
    auto url = url_r.unwrap();
    bool tls = (url.scheme == "wss");
    u16 port = url.port != 0 ? url.port : (tls ? 443u : 80u);
    std::string path = url.path.empty() ? "/" : url.path;
    if (!url.query.empty()) path += "?" + url.query;
    std::string host = url.host;
    if (!host.empty() && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);

    auto r = co_await connect(host, port, path, tls);
    co_return r;
}

async::task<bool> WebSocket::connect(const std::string& host, u16 port, const std::string& path, bool tls) {
    close_sync();
    use_tls_ = tls;
    tcp_ = std::make_unique<Connection>();

    ConnectionConfig cfg;
    cfg.connect_timeout_ms = 10000;
    cfg.read_timeout_ms = 30000;

    auto open_r = co_await tcp_->open_async(host, port, cfg);
    if (open_r.is_err()) co_return std::string("connect: ") + open_r.unwrap_err();

    if (use_tls_) {
        tls_ = std::make_unique<tls::TLSConnection>();
        auto tr = co_await tls_->connect_async(tcp_.get(), host);
        if (tr.is_err()) { close_sync(); co_return std::string("tls: ") + tr.unwrap_err(); }
    }

    auto handshake_r = co_await perform_handshake(host, port, path);
    if (handshake_r.is_err()) { close_sync(); co_return handshake_r.unwrap_err(); }

    connected_ = true;
    co_return true;
}

async::task<bool> WebSocket::perform_handshake(const std::string& host, u16 port, const std::string& path) {
    auto key = generate_key();

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host;
    if (port != 80 && port != 443) req << ":" << port;
    req << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";

    auto req_str = req.str();

    if (use_tls_) {
        auto r = co_await tls_->send_all_async(reinterpret_cast<const u8*>(req_str.data()), static_cast<u32>(req_str.size()));
        if (r.is_err()) co_return std::string("send handshake: ") + r.unwrap_err();
    } else {
        auto r = co_await tcp_->send_all_async(reinterpret_cast<const u8*>(req_str.data()), static_cast<u32>(req_str.size()));
        if (r.is_err()) co_return std::string("send handshake: ") + r.unwrap_err();
    }

    std::string response;
    u8 buf[1];
    bool found_end = false;
    while (!found_end) {
        auto r = use_tls_ ? co_await tls_->receive_async(buf, 1) : co_await tcp_->receive_async(buf, 1);
        if (r.is_err()) co_return std::string("recv handshake: ") + r.unwrap_err();
        u32 n = r.unwrap();
        if (n == 0) co_return std::string("connection closed during handshake");
        response += static_cast<char>(buf[0]);
        if (response.size() >= 4 &&
            response[response.size() - 4] == '\r' && response[response.size() - 3] == '\n' &&
            response[response.size() - 2] == '\r' && response[response.size() - 1] == '\n') {
            found_end = true;
        }
        if (response.size() > 4096) co_return std::string("handshake too large");
    }

    auto crlf1 = response.find("\r\n");
    if (crlf1 == std::string::npos) co_return std::string("no status line");
    auto status_line = response.substr(0, crlf1);
    auto sp1 = status_line.find(' ');
    if (sp1 == std::string::npos) co_return std::string("bad status line");
    auto sp2 = status_line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) co_return std::string("bad status line");
    std::string code_str = status_line.substr(sp1 + 1, sp2 - sp1 - 1);
    if (code_str != "101") co_return std::string("bad status: ") + code_str;

    auto headers_end = response.find("\r\n\r\n");
    if (headers_end == std::string::npos) co_return std::string("no header end");
    auto header_section = response.substr(crlf1 + 2, headers_end - crlf1 - 2);

    bool has_upgrade = false, has_accept = false;
    std::string accept_key;

    std::istringstream hstream(header_section);
    std::string line;
    while (std::getline(hstream, line)) {
        if (line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 2);
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == "upgrade" && value.find("websocket") != std::string::npos) has_upgrade = true;
        if (name == "sec-websocket-accept") { has_accept = true; accept_key = value; }
    }

    if (!has_upgrade) co_return std::string("no upgrade header");
    if (!has_accept) co_return std::string("no accept key");

    if (accept_key != compute_accept_key(key)) co_return std::string("accept key mismatch");

    co_return true;
}

std::vector<u8> WebSocket::encode_frame(const WebSocketFrame& frame) {
    std::vector<u8> data;
    data.push_back(static_cast<u8>((frame.fin ? 0x80 : 0x00) | static_cast<u8>(frame.opcode)));

    u64 len = frame.payload_length;
    if (len < 126) {
        data.push_back(static_cast<u8>(len) | (frame.masked ? 0x80 : 0x00));
    } else if (len <= 0xFFFF) {
        data.push_back(126 | (frame.masked ? 0x80 : 0x00));
        data.push_back(static_cast<u8>((len >> 8) & 0xFF));
        data.push_back(static_cast<u8>(len & 0xFF));
    } else {
        data.push_back(127 | (frame.masked ? 0x80 : 0x00));
        for (int i = 7; i >= 0; i--)
            data.push_back(static_cast<u8>((len >> (i * 8)) & 0xFF));
    }

    if (frame.masked) {
        data.insert(data.end(), frame.mask_key, frame.mask_key + 4);
    }

    if (len > 0) {
        std::vector<u8> payload = frame.payload;
        if (frame.masked && len > 0) {
            for (u64 i = 0; i < len; i++)
                payload[i] ^= frame.mask_key[i % 4];
        }
        data.insert(data.end(), payload.begin(), payload.end());
    }

    return data;
}

Result<WebSocketFrame> WebSocket::decode_frame(const u8* data, u32 len, u32& consumed) {
    WebSocketFrame frame = {};
    consumed = 0;

    if (len < 2) return std::string("frame too short");

    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = static_cast<Opcode>(data[0] & 0x0F);
    frame.masked = (data[1] & 0x80) != 0;

    u32 pos = 2;
    u64 payload_len = data[1] & 0x7F;
    if (payload_len == 126) {
        if (pos + 2 > len) return std::string("frame truncated at 16-bit len");
        payload_len = (static_cast<u64>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
    } else if (payload_len == 127) {
        if (pos + 8 > len) return std::string("frame truncated at 64-bit len");
        payload_len = 0;
        for (int i = 0; i < 8; i++)
            payload_len = (payload_len << 8) | data[pos + i];
        pos += 8;
    }

    if (frame.masked) {
        if (pos + 4 > len) return std::string("frame truncated at mask");
        std::memcpy(frame.mask_key, data + pos, 4);
        pos += 4;
    }

    frame.payload_length = payload_len;
    if (pos + payload_len > len) return std::string("payload truncated");

    frame.payload.assign(data + pos, data + pos + static_cast<u32>(payload_len));
    if (frame.masked) {
        for (u64 i = 0; i < payload_len; i++)
            frame.payload[i] ^= frame.mask_key[i % 4];
    }

    consumed = pos + static_cast<u32>(payload_len);
    return frame;
}

async::task<bool> WebSocket::send_raw(span<u8> data) {
    if (use_tls_) {
        auto r = co_await tls_->send_all_async(data.data(), data.size());
        co_return r;
    }
    auto r = co_await tcp_->send_all_async(data.data(), data.size());
    co_return r;
}

async::task<u32> WebSocket::recv_raw(u8* buf, u32 len) {
    if (use_tls_) {
        auto r = co_await tls_->receive_async(buf, len);
        co_return r;
    }
    auto r = co_await tcp_->receive_async(buf, len);
    co_return r;
}

async::task<bool> WebSocket::send_text(const std::string& text) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::TEXT;
    frame.masked = true;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; i++) frame.mask_key[i] = static_cast<u8>(dis(gen));
    frame.payload_length = text.size();
    frame.payload.assign(reinterpret_cast<const u8*>(text.data()), reinterpret_cast<const u8*>(text.data()) + text.size());
    auto r = co_await send_frame(frame);
    co_return r;
}

async::task<bool> WebSocket::send_binary(span<u8> data) {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::BINARY;
    frame.masked = true;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; i++) frame.mask_key[i] = static_cast<u8>(dis(gen));
    frame.payload_length = data.size();
    frame.payload.assign(data.data(), data.data() + data.size());
    auto r = co_await send_frame(frame);
    co_return r;
}

async::task<bool> WebSocket::send_frame(const WebSocketFrame& frame) {
    auto encoded = encode_frame(frame);
    auto r = co_await send_raw(span<u8>(encoded.data(), static_cast<u32>(encoded.size())));
    co_return r;
}

async::task<bool> WebSocket::send_ping() {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::PING;
    frame.masked = true;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; i++) frame.mask_key[i] = static_cast<u8>(dis(gen));
    frame.payload_length = 0;
    auto r = co_await send_frame(frame);
    co_return r;
}

async::task<bool> WebSocket::send_pong() {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::PONG;
    frame.masked = true;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; i++) frame.mask_key[i] = static_cast<u8>(dis(gen));
    frame.payload_length = 0;
    auto r = co_await send_frame(frame);
    co_return r;
}

async::task<bool> WebSocket::send_close(CloseCode code, const std::string& reason) {
    if (closing_) co_return true;
    closing_ = true;

    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::CLOSE;
    frame.masked = true;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 4; i++) frame.mask_key[i] = static_cast<u8>(dis(gen));

    std::vector<u8> payload(2);
    payload[0] = static_cast<u8>((static_cast<u16>(code) >> 8) & 0xFF);
    payload[1] = static_cast<u8>(static_cast<u16>(code) & 0xFF);
    payload.insert(payload.end(), reason.begin(), reason.end());
    frame.payload_length = payload.size();
    frame.payload = std::move(payload);

    auto r = co_await send_frame(frame);
    connected_ = false;
    co_return r;
}

async::task<WebSocketFrame> WebSocket::receive_frame() {
    u8 header[10];
    u32 hdr_pos = 0;

    while (hdr_pos < 2) {
        auto r = co_await recv_raw(header + hdr_pos, 2 - hdr_pos);
        if (r.is_err()) co_return std::string("recv header: ") + r.unwrap_err();
        u32 n = r.unwrap();
        if (n == 0) co_return std::string("connection closed");
        hdr_pos += n;
    }

    u8 first_len_byte = header[1] & 0x7F;
    u32 extra_header = 0;
    if (first_len_byte == 126) extra_header = 2;
    else if (first_len_byte == 127) extra_header = 8;
    if (header[1] & 0x80) extra_header += 4;

    while (hdr_pos < 2 + extra_header) {
        auto r = co_await recv_raw(header + hdr_pos, 2 + extra_header - hdr_pos);
        if (r.is_err()) co_return std::string("recv ext header: ") + r.unwrap_err();
        u32 n = r.unwrap();
        if (n == 0) co_return std::string("connection closed");
        hdr_pos += n;
    }

    u32 consumed = 0;
    auto frame_r = decode_frame(header, hdr_pos, consumed);
    if (frame_r.is_err()) co_return frame_r.unwrap_err();
    auto frame = frame_r.unwrap();

    if (frame.payload_length > 0) {
        frame.payload.resize(static_cast<std::size_t>(frame.payload_length));
        u32 got = 0;
        while (got < frame.payload_length) {
            auto r = co_await recv_raw(frame.payload.data() + got, static_cast<u32>(frame.payload_length - got));
            if (r.is_err()) co_return std::string("recv payload: ") + r.unwrap_err();
            u32 n = r.unwrap();
            if (n == 0) co_return std::string("connection closed");
            got += n;
        }

        if (frame.masked) {
            for (u64 i = 0; i < frame.payload_length; i++)
                frame.payload[i] ^= frame.mask_key[i % 4];
        }
    }

    if (frame.opcode == Opcode::PING) {
        WebSocketFrame pong;
        pong.fin = true;
        pong.opcode = Opcode::PONG;
        pong.masked = true;
        pong.payload_length = frame.payload_length;
        pong.payload = frame.payload;
        std::memcpy(pong.mask_key, frame.mask_key, 4);
        co_await send_frame(pong);
        auto next = co_await receive_frame();
        co_return next;
    }

    if (frame.opcode == Opcode::CLOSE) {
        connected_ = false;
        closing_ = true;
        co_await send_close(CloseCode::NORMAL);
        if (on_close_cb_) {
            CloseCode cc = CloseCode::NORMAL;
            if (frame.payload.size() >= 2) {
                cc = static_cast<CloseCode>((static_cast<u16>(frame.payload[0]) << 8) | frame.payload[1]);
            }
            std::string reason;
            if (frame.payload.size() > 2)
                reason.assign(reinterpret_cast<const char*>(frame.payload.data() + 2), frame.payload.size() - 2);
            on_close_cb_(cc, reason);
        }
        co_return frame;
    }

    if (frame.opcode == Opcode::TEXT || frame.opcode == Opcode::BINARY) {
        if (on_message_cb_ && frame.opcode == Opcode::TEXT) {
            on_message_cb_(std::string(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size()));
        }
    }

    co_return frame;
}

async::task<std::vector<u8>> WebSocket::receive_text() {
    auto frame_r = co_await receive_frame();
    if (frame_r.is_err()) co_return std::string("not text");
    auto frame = frame_r.unwrap();
    if (frame.opcode != Opcode::TEXT) co_return std::string("not text frame");
    co_return frame.payload;
}

async::task<bool> WebSocket::close(CloseCode code, const std::string& reason) {
    auto r = co_await send_close(code, reason);
    co_return r;
}

} // namespace browser::net::ws

