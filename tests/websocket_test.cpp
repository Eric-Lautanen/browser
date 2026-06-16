#include "test_framework.hpp"
#include "utility.hpp"
#include "../net/websocket.hpp"
#include "../net/url.hpp"
#include <cstring>
#include <string>

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
    frame.payload.assign((const browser::u8*)msg.data(), (const browser::u8*)msg.data() + 5);

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
    std::string decoded_msg((const char*)decoded.payload.data(), decoded.payload.size());
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
    browser::u8 close_payload[] = {0x03, 0xE8}; // 1000 = NORMAL
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
    frame.payload.assign((const browser::u8*)msg.data(), (const browser::u8*)msg.data() + 200);

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
    frame.payload.assign((const browser::u8*)msg.data(), (const browser::u8*)msg.data() + msg.size());

    auto encoded = WebSocket::encode_frame(frame);
    browser::u32 consumed = 0;
    auto decoded_r = WebSocket::decode_frame(encoded.data(), (browser::u32)encoded.size(), consumed);
    ASSERT(decoded_r.is_ok());
    auto decoded = decoded_r.unwrap();
    std::string decoded_msg((const char*)decoded.payload.data(), decoded.payload.size());
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
