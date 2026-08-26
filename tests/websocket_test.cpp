#include "../net/websocket.hpp"

#include "../net/url.hpp"
#include "test_framework.hpp"
#include "utility.hpp"

#include <cstring>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace browser;
using namespace browser::net::ws;

TEST(websocket_encode_decode_text, {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::TEXT;
    frame.masked = true;
    frame.mask_key[0] = 0x01;
    frame.mask_key[1] = 0x02;
    frame.mask_key[2] = 0x03;
    frame.mask_key[3] = 0x04;
    std::string msg = "Hello";
    frame.payload_length = 5;
    frame.payload.assign((const browser::u8 *)msg.data(), (const browser::u8 *)msg.data() + 5);

    auto encoded = WebSocket::encode_frame(frame);
    ASSERT(encoded.size() > 0);

    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    ASSERT(decoded.fin == true);
    ASSERT(decoded.opcode == Opcode::TEXT);
    ASSERT(decoded.masked == true);
    ASSERT(decoded.payload_length == 5);
    // Unmask
    std::string decoded_msg((const char *)decoded.payload.data(), decoded.payload.size());
    ASSERT(decoded_msg == "Hello");
    ASSERT(consumed == encoded.size());
})

TEST(websocket_encode_decode_binary, {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::BINARY;
    frame.masked = false;
    browser::u8 data[] = {0x00, 0x01, 0x02, 0x03};
    frame.payload_length = 4;
    frame.payload.assign(data, data + 4);

    auto encoded = WebSocket::encode_frame(frame);
    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    ASSERT(decoded.opcode == Opcode::BINARY);
    ASSERT(decoded.payload_length == 4);
    ASSERT(consumed == encoded.size());
})

TEST(websocket_encode_decode_close, {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::CLOSE;
    frame.masked = false;
    browser::u8 close_payload[] = {0x03, 0xE8};  // 1000 = NORMAL
    frame.payload_length = 2;
    frame.payload.assign(close_payload, close_payload + 2);

    auto encoded = WebSocket::encode_frame(frame);
    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    ASSERT(decoded.opcode == Opcode::CLOSE);
    ASSERT(decoded.payload_length == 2);
})

TEST(websocket_long_message, {
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::TEXT;
    frame.masked = false;

    // Message longer than 126 bytes to test extended length
    std::string msg(200, 'A');
    frame.payload_length = 200;
    frame.payload.assign((const browser::u8 *)msg.data(), (const browser::u8 *)msg.data() + 200);

    auto encoded = WebSocket::encode_frame(frame);
    ASSERT(encoded.size() > 200);

    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    ASSERT(decoded.payload_length == 200);
    ASSERT(consumed == encoded.size());
})

TEST(websocket_masking, {
    // Test that masking/unmasking works correctly
    WebSocketFrame frame;
    frame.fin = true;
    frame.opcode = Opcode::TEXT;
    frame.masked = true;
    frame.mask_key[0] = 0x37;
    frame.mask_key[1] = 0xfa;
    frame.mask_key[2] = 0x21;
    frame.mask_key[3] = 0x3d;
    std::string msg = "Hello, WebSocket!";
    frame.payload_length = static_cast<browser::u64>(msg.size());
    frame.payload.assign((const browser::u8 *)msg.data(), (const browser::u8 *)msg.data() + msg.size());

    auto encoded = WebSocket::encode_frame(frame);
    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    std::string decoded_msg((const char *)decoded.payload.data(), decoded.payload.size());
    ASSERT(decoded_msg == msg);
})

TEST(websocket_ping_pong, {
    WebSocketFrame ping;
    ping.fin = true;
    ping.opcode = Opcode::PING;
    ping.masked = false;
    ping.payload_length = 0;

    auto encoded = WebSocket::encode_frame(ping);
    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    ASSERT(decoded.opcode == Opcode::PING);
})

// N-S11: a peer-declared 64-bit payload length must not trigger a huge
// allocation; frames over the cap are rejected before any resize.
TEST(websocket_oversized_payload_rejected, {
    // Header claiming a 64-bit length of 4 GB (0x1_0000_0000), no payload.
    std::vector<browser::u8> frame = {0x82, 0x7F, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    browser::u32 consumed = 0;
    auto r = WebSocket::decode_frame(frame.data(), (browser::u32)frame.size(), consumed);
    ASSERT(r.is_err());
})

// ---- N-C14: reserved bits / reserved opcodes / control-frame rules ----

TEST(websocket_reserved_bits_rejected, {
    // RSV1 set on a text frame — no extension was negotiated.
    std::vector<browser::u8> frame = {0xC1, 0x00};  // FIN|RSV1|TEXT
    browser::u32 consumed = 0;
    auto r = WebSocket::decode_frame(frame.data(), (browser::u32)frame.size(), consumed);
    ASSERT(r.is_err());
})

TEST(websocket_reserved_opcodes_rejected, {
    std::vector<browser::u8> frames[] = {
        {0x83, 0x00},  // opcode 3 (reserved data)
        {0x87, 0x00},  // opcode 7 (reserved data)
        {0x8B, 0x00},  // opcode 0xB (reserved control)
    };
    for (auto &f : frames) {
        browser::u32 consumed = 0;
        auto r = WebSocket::decode_frame(f.data(), (browser::u32)f.size(), consumed);
        ASSERT(r.is_err());
    }
})

TEST(websocket_control_frame_rules_enforced, {
    // Fragmented control frame (FIN=0 on PING) is invalid.
    std::vector<browser::u8> frag_ping = {0x09, 0x00};
    // Control frame with extended length encoding is invalid.
    std::vector<browser::u8> ext_close = {0x88, 0x7E, 0x00, 0x05};
    for (auto *f : {&frag_ping, &ext_close}) {
        browser::u32 consumed = 0;
        auto r = WebSocket::decode_frame(f->data(), (browser::u32)f->size(), consumed);
        ASSERT(r.is_err());
    }
})

// ---- N-C14: SHA-1 update() corrupted every multi-block hash. The accept-key
// path feeds key+GUID; a long key forces multiple blocks through update(). ----

TEST(websocket_accept_key_multi_block_sha1, {
    // 40-char key + 36-byte GUID = 76 bytes => spans more than one block.
    std::string key(40, 'A');
    std::string accept = WebSocket::compute_accept_key(key);
    // Ground truth via Python hashlib:
    // base64(sha1(("A"*40 + GUID).encode()))
    ASSERT(accept == "NxYEIl52w+su0lh863QC70fyRyc=");
    // RFC 6455 section 1.3 example still holds (single-block input).
    ASSERT(WebSocket::compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
})

// ---- N-C5 end-to-end: a real server delivering payloads, interleaved PING
// and fragmented messages. The old receive path could never deliver any
// payload ("payload truncated" on every data frame). ----

static const char kWsClientKey[] = "AAAAAAAAAAAAAAAAAAAAAA==";

static void ws_server_thread(SOCKET listener) {
    SOCKET c = ::accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET)
        return;

    // Read the client's HTTP upgrade request (until CRLFCRLF).
    char buf[2048];
    std::string req;
    for (;;) {
        int n = ::recv(c, buf, sizeof(buf), 0);
        if (n <= 0) {
            ::closesocket(c);
            return;
        }
        req.append(buf, buf + n);
        if (req.find("\r\n\r\n") != std::string::npos)
            break;
        if (req.size() > 4096) {
            ::closesocket(c);
            return;
        }
    }

    // Extract the client's Sec-WebSocket-Key and answer with a valid accept.
    auto key_pos = req.find("Sec-WebSocket-Key: ");
    if (key_pos == std::string::npos) {
        ::closesocket(c);
        return;
    }
    key_pos += strlen("Sec-WebSocket-Key: ");
    auto key_end = req.find("\r\n", key_pos);
    if (key_end == std::string::npos) {
        ::closesocket(c);
        return;
    }
    std::string client_key = req.substr(key_pos, key_end - key_pos);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        WebSocket::compute_accept_key(client_key) + "\r\n\r\n";
    ::send(c, resp.data(), (int)resp.size(), 0);

    // Interleaved PING (must not stall the message flow).
    const unsigned char ping[] = {0x89, 0x00};
    ::send(c, (const char *)ping, sizeof(ping), 0);

    // Fragmented TEXT: "Hello, " (no FIN) + CONTINUATION/FIN "world!".
    const unsigned char frag1[] = {0x01, 0x07, 'H', 'e', 'l', 'l', 'o', ',', ' '};
    const unsigned char frag2[] = {0x80, 0x06, 'w', 'o', 'r', 'l', 'd', '!'};
    ::send(c, (const char *)frag1, sizeof(frag1), 0);
    ::send(c, (const char *)frag2, sizeof(frag2), 0);

    // Complete unmasked TEXT frame.
    const char *text = "hello websocket";
    unsigned char frame[2 + 15] = {0x81, static_cast<unsigned char>(strlen(text))};
    std::memcpy(frame + 2, text, strlen(text));
    ::send(c, (const char *)frame, sizeof(frame), 0);

    // Give the client time to read before closing.
    Sleep(300);
    ::closesocket(c);
}

TEST(websocket_receive_payloads_end_to_end, {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(listener != INVALID_SOCKET);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT(::bind(listener, (sockaddr *)&addr, sizeof(addr)) == 0);
    sockaddr_in bound{};
    int blen = sizeof(bound);
    ::getsockname(listener, (sockaddr *)&bound, &blen);
    u16 port = ntohs(bound.sin_port);
    ASSERT(::listen(listener, 1) == 0);

    std::thread server(ws_server_thread, listener);
    struct Joiner {
        std::thread &t;
        SOCKET l;
        ~Joiner() {
            ::closesocket(l);
            if (t.joinable())
                t.join();
        }
    } joiner{server, listener};

    WebSocket sock;
    auto conn_r = sock.connect("127.0.0.1", port, "/", false).sync_wait();
    ASSERT(conn_r.is_ok());
    ASSERT(sock.is_connected());

    // Frame 1: the interleaved PING is answered transparently; we get the
    // assembled fragmented message "Hello, world!".
    auto f1_r = sock.receive_frame().sync_wait();
    ASSERT(f1_r.is_ok());
    auto f1 = f1_r.unwrap();
    ASSERT(f1.opcode == Opcode::TEXT);
    std::string msg1((const char *)f1.payload.data(), f1.payload.size());
    if (msg1 != "Hello, world!") {
        _err = "msg1=\"" + msg1 + "\" len=" + std::to_string(msg1.size());
        return false;
    }

    // Frame 2: the complete text frame.
    auto f2_r = sock.receive_frame().sync_wait();
    ASSERT(f2_r.is_ok());
    auto f2 = f2_r.unwrap();
    ASSERT(f2.opcode == Opcode::TEXT);
    std::string msg2((const char *)f2.payload.data(), f2.payload.size());
    ASSERT(msg2 == "hello websocket");

    sock.close_sync();
})
