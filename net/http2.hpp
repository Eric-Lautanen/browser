#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "url.hpp"
#include "connection.hpp"
#include "tls.hpp"
#include "http.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

namespace browser::net::http2 {

// Windows headers define NO_ERROR as a macro; undefine it
#ifdef NO_ERROR
#undef NO_ERROR
#endif

enum FrameType : u8 {
    DATA = 0,
    HEADERS = 1,
    PRIORITY = 2,
    RST_STREAM = 3,
    SETTINGS = 4,
    PUSH_PROMISE = 5,
    PING = 6,
    GOAWAY = 7,
    WINDOW_UPDATE = 8,
    CONTINUATION = 9
};

struct FrameHeader {
    u32 length;
    FrameType type;
    u8 flags;
    u32 stream_id;
};

// HTTP/2 error codes
enum ErrorCode : u32 {
    NO_ERROR = 0,
    PROTOCOL_ERROR = 1,
    INTERNAL_ERROR = 2,
    FLOW_CONTROL_ERROR = 3,
    SETTINGS_TIMEOUT = 4,
    STREAM_CLOSED = 5,
    FRAME_SIZE_ERROR = 6,
    REFUSED_STREAM = 7,
    CANCEL = 8,
    COMPRESSION_ERROR = 9,
    CONNECT_ERROR = 10,
    ENHANCE_YOUR_CALM = 11,
    INADEQUATE_SECURITY = 12,
    HTTP_1_1_REQUIRED = 13
};

// Settings identifiers
enum SettingsId : u16 {
    SETTINGS_HEADER_TABLE_SIZE = 1,
    SETTINGS_ENABLE_PUSH = 2,
    SETTINGS_MAX_CONCURRENT_STREAMS = 3,
    SETTINGS_INITIAL_WINDOW_SIZE = 4,
    SETTINGS_MAX_FRAME_SIZE = 5,
    SETTINGS_MAX_HEADER_LIST_SIZE = 6
};

// --- HPack ---

struct HPackEntry {
    std::string name;
    std::string value;
};

class HPack {
public:
    HPack();
    ~HPack();
    HPack(HPack&&) noexcept;
    HPack& operator=(HPack&&) noexcept;
    HPack(const HPack&) = delete;

    std::vector<HPackEntry> decode(const u8* data, u32 len);
    std::vector<u8> encode(const std::vector<HPackEntry>& headers);

    void set_max_table_size(u32 size);
    u32 max_table_size() const { return max_table_size_; }

    // Static utility methods (public for testing)
    static u32 decode_integer(const u8* data, u32 len, u32& pos, u8 prefix_bits);
    static std::vector<u8> encode_integer(u32 value, u8 prefix_bits);
    static std::string decode_string(const u8* data, u32 len, u32& pos);
    static std::vector<u8> encode_string(const std::string& s);
    static std::string huffman_decode(const u8* data, u32 len);
    const HPackEntry* get_entry(u32 index) const;

private:
    static const HPackEntry kStaticTable[61];

    std::vector<HPackEntry> dynamic_table_;
    u32 max_table_size_ = 4096;
    u32 current_table_size_ = 0;

    u32 find_in_table(const std::string& name, const std::string& value) const;
    u32 find_name_in_table(const std::string& name) const;
    void evict_to_fit(u32 new_entry_size);
};

// --- HTTP2Client ---

class HTTP2Client {
public:
    HTTP2Client();
    ~HTTP2Client();
    HTTP2Client(HTTP2Client&&) noexcept;
    HTTP2Client& operator=(HTTP2Client&&) noexcept;
    HTTP2Client(const HTTP2Client&) = delete;

    Result<void> connect(const std::string& host, u16 port, bool use_tls,
                         Connection* existing_tcp = nullptr,
                         tls::TLSConnection* existing_tls = nullptr);
    Result<http::Response> execute(const http::Request& req);
    void close();
    bool is_connected() const;

    async::task<http::Response> execute_async(const http::Request& req);

private:
    Connection tcp_;
    std::unique_ptr<tls::TLSConnection> tls_;
    bool use_tls_ = false;
    bool connected_ = false;
    u32 next_stream_id_ = 1;
    HPack hpack_;

    // Connection flow control
    u32 server_window_ = 65535;
    u32 client_window_ = 65535;

    // Server settings
    u32 server_max_frame_size_ = 16384;
    u32 server_header_table_size_ = 4096;
    u32 server_initial_window_size_ = 65535;
    bool server_enable_push_ = true;

public:
    // Frame I/O (public for testing)
    static std::vector<u8> serialize_frame(const FrameHeader& hdr, const u8* payload);
    static Result<FrameHeader> parse_frame_header(const u8* data, u32 len, u32& pos);

    Result<void> send_frame(FrameType type, u8 flags, u32 stream_id, const std::vector<u8>& payload);
    Result<FrameHeader> read_frame();
    Result<u32> read_frame_payload(const FrameHeader& hdr, std::vector<u8>& out);
    Result<u32> read_some(u8* buf, u32 len);

private:
    // Protocol flow
    Result<void> send_preface();
    Result<void> send_settings(const std::vector<std::pair<u16, u32>>& settings);
    Result<void> send_window_update(u32 stream_id, u32 increment);
    Result<void> send_goaway(u32 last_stream_id, u32 error_code);

    Result<void> read_and_process_settings();

    // HTTP/2 pseudo-header helpers
    static std::vector<HPackEntry> request_to_hpack(const http::Request& req);
    static Result<http::Response> hpack_to_response(const std::vector<HPackEntry>& entries);
};

} // namespace browser::net::http2
