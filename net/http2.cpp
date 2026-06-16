#include "http2.hpp"
#include <cstring>
#include <algorithm>
#include <memory>
#include <sstream>

namespace browser::net::http2 {

// ============================================================================
// Utility: big-endian read/write helpers
// ============================================================================

static u32 read_u32_be(const u8* data, u32 len, u32& pos) {
    if (pos + 4 > len) return 0;
    u32 v = (static_cast<u32>(data[pos]) << 24) |
            (static_cast<u32>(data[pos + 1]) << 16) |
            (static_cast<u32>(data[pos + 2]) << 8) |
            data[pos + 3];
    pos += 4;
    return v;
}

static u32 peek_u24_be(const u8* data, u32 len, u32 pos) {
    if (pos + 3 > len) return 0;
    return (static_cast<u32>(data[pos]) << 16) |
           (static_cast<u32>(data[pos + 1]) << 8) |
           data[pos + 2];
}

static void write_u16_be(std::vector<u8>& out, u16 v) {
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>(v & 0xFF));
}

static void write_u32_be(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>((v >> 24) & 0xFF));
    out.push_back(static_cast<u8>((v >> 16) & 0xFF));
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>(v & 0xFF));
}

static void write_u24_be(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>((v >> 16) & 0xFF));
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>(v & 0xFF));
}

// ============================================================================
// Frame header serialization
// ============================================================================

std::vector<u8> HTTP2Client::serialize_frame(const FrameHeader& hdr, const u8* payload) {
    std::vector<u8> frame;
    // 9-byte frame header: length(24b), type(8b), flags(8b), stream_id(31b+R bit)
    write_u24_be(frame, hdr.length);
    frame.push_back(static_cast<u8>(hdr.type));
    frame.push_back(hdr.flags);
    // stream_id: 31 bits, MSB is reserved (R bit = 0)
    write_u32_be(frame, hdr.stream_id & 0x7FFFFFFFu);
    if (payload && hdr.length > 0)
        frame.insert(frame.end(), payload, payload + hdr.length);
    return frame;
}

Result<FrameHeader> HTTP2Client::parse_frame_header(const u8* data, u32 len, u32& pos) {
    if (pos + 9 > len) return std::string("frame header truncated");
    FrameHeader hdr;
    hdr.length = peek_u24_be(data, len, pos); pos += 3;
    if (pos >= len) return std::string("frame header truncated at type");
    hdr.type = static_cast<FrameType>(data[pos++]);
    if (pos >= len) return std::string("frame header truncated at flags");
    hdr.flags = data[pos++];
    hdr.stream_id = read_u32_be(data, len, pos) & 0x7FFFFFFFu;
    return hdr;
}

// ============================================================================
// HPack: Integer Encoding (RFC 7541 §5.1)
// ============================================================================

u32 HPack::decode_integer(const u8* data, u32 len, u32& pos, u8 prefix_bits) {
    if (pos >= len) return 0;
    u8 prefix_mask = static_cast<u8>((1 << prefix_bits) - 1);
    u32 value = data[pos] & prefix_mask;
    if (value < static_cast<u32>(prefix_mask)) {
        pos++;
        return value;
    }
    pos++;
    u32 shift = 0;
    u32 cont_bytes = 0;
    while (true) {
        if (pos >= len) return value;
        if (cont_bytes >= 5) return value; // limit continuation to avoid DoS
        u8 b = data[pos];
        value += static_cast<u32>(b & 0x7F) << shift;
        shift += 7;
        pos++;
        cont_bytes++;
        if (!(b & 0x80)) break;
    }
    return value;
}

std::vector<u8> HPack::encode_integer(u32 value, u8 prefix_bits) {
    std::vector<u8> out;
    u8 prefix_mask = static_cast<u8>((1 << prefix_bits) - 1);
    if (value < static_cast<u32>(prefix_mask)) {
        out.push_back(static_cast<u8>(value));
    } else {
        out.push_back(prefix_mask);
        value -= prefix_mask;
        while (value >= 128) {
            out.push_back(static_cast<u8>((value & 0x7F) | 0x80));
            value >>= 7;
        }
        out.push_back(static_cast<u8>(value & 0x7F));
    }
    return out;
}

// ============================================================================
// HPack: Huffman Decoding (RFC 7541 Appendix B)
// ============================================================================

struct HuffmanSymbol {
    u32 code;
    u8 bits;
    u16 symbol;
};

// The 257-entry Huffman table from RFC 7541 Appendix B
// Generated from the specification: symbols 0-255 plus EOS at index 256
static const HuffmanSymbol kHuffmanTable[257] = {
#include "huffman_table.inc"
};

struct HuffmanNode {
    u16 symbol; // 0-255 for leaf, 256 for internal node
    std::unique_ptr<HuffmanNode> child[2];

    HuffmanNode() : symbol(256) {}
};

// Build a binary trie from the Huffman table
static std::unique_ptr<HuffmanNode> build_huffman_trie() {
    auto root = std::make_unique<HuffmanNode>();
    for (int sym = 0; sym < 256; sym++) {
        u32 code = kHuffmanTable[sym].code;
        u8 bits = kHuffmanTable[sym].bits;
        if (bits == 0) continue;
        HuffmanNode* node = root.get();
        for (int b = static_cast<int>(bits) - 1; b >= 0; b--) {
            u8 bit = static_cast<u8>((code >> b) & 1);
            if (!node->child[bit])
                node->child[bit] = std::make_unique<HuffmanNode>();
            node = node->child[bit].get();
        }
        node->symbol = static_cast<u16>(sym);
    }
    return root;
}

static HuffmanNode* get_huffman_trie() {
    static auto trie = build_huffman_trie();
    return trie.get();
}

std::string HPack::huffman_decode(const u8* data, u32 len) {
    if (len == 0) return {};
    std::string result;
    auto* trie = get_huffman_trie();
    u64 bit_pos = 0;
    u64 total_bits = static_cast<u64>(len) * 8;
    while (bit_pos < total_bits) {
        HuffmanNode* node = trie;
        u64 start_pos = bit_pos;
        while (bit_pos < total_bits) {
            u64 byte_off = bit_pos >> 3;
            u8 bit_off = static_cast<u8>(bit_pos & 7);
            u8 bit = static_cast<u8>((data[byte_off] >> (7 - bit_off)) & 1);
            bit_pos++;
            node = node->child[bit].get();
            if (!node) {
                // Invalid path — validate as padding
                u64 padding_bits = total_bits - start_pos;
                if (padding_bits > 7) return {};
                for (u64 b = 0; b < padding_bits; b++) {
                    u64 bo = (start_pos + b) >> 3;
                    u8 bi = static_cast<u8>((start_pos + b) & 7);
                    u8 val = static_cast<u8>((data[bo] >> (7 - bi)) & 1);
                    if (val == 0) return {};
                }
                return result;
            }
            if (node->symbol <= 255) {
                result += static_cast<char>(node->symbol);
                goto next;
            }
        }
        // Ran out of bits without a leaf — validate as padding
        {
            u64 padding_bits = total_bits - start_pos;
            if (padding_bits > 7) return {};
            for (u64 b = 0; b < padding_bits; b++) {
                u64 bo = (start_pos + b) >> 3;
                u8 bi = static_cast<u8>((start_pos + b) & 7);
                u8 val = static_cast<u8>((data[bo] >> (7 - bi)) & 1);
                if (val == 0) return {};
            }
            return result;
        }
        next:;
    }
    return result;
}

// ============================================================================
// HPack: String Decoding (RFC 7541 §5.2)
// ============================================================================

std::string HPack::decode_string(const u8* data, u32 len, u32& pos) {
    if (pos >= len) return {};
    u8 first = data[pos];
    bool huffman = (first & 0x80) != 0;
    u32 str_len = decode_integer(data, len, pos, 7);
    if (pos + str_len > len) {
        pos = len;
        return {};
    }
    std::string result;
    if (huffman) {
        result = huffman_decode(data + pos, str_len);
    } else {
        result.assign(reinterpret_cast<const char*>(data + pos), str_len);
    }
    pos += str_len;
    return result;
}

std::vector<u8> HPack::encode_string(const std::string& s) {
    // Encode as plain string (Huffman flag = 0)
    auto len_enc = encode_integer(static_cast<u32>(s.size()), 7);
    std::vector<u8> out;
    // First byte: H bit (0) | length prefix
    out.push_back(len_enc[0]);
    for (std::size_t i = 1; i < len_enc.size(); i++)
        out.push_back(len_enc[i]);
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

// ============================================================================
// HPack: Static Table (RFC 7541 Appendix A) — 61 entries
// ============================================================================

const HPackEntry HPack::kStaticTable[61] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", ""},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""}
};

// ============================================================================
// HPack: Table Lookup
// ============================================================================

const HPackEntry* HPack::get_entry(u32 index) const {
    if (index == 0) return nullptr;
    if (index <= 61) return &kStaticTable[index - 1];
    u32 dyn_idx = index - 62;
    if (dyn_idx < dynamic_table_.size())
        return &dynamic_table_[dyn_idx];
    return nullptr;
}

u32 HPack::find_name_in_table(const std::string& name) const {
    for (u32 i = 0; i < 61; i++) {
        if (kStaticTable[i].name == name)
            return i + 1;
    }
    for (u32 i = 0; i < dynamic_table_.size(); i++) {
        if (dynamic_table_[i].name == name)
            return 62 + i;
    }
    return 0;
}

u32 HPack::find_in_table(const std::string& name, const std::string& value) const {
    for (u32 i = 0; i < 61; i++) {
        if (kStaticTable[i].name == name && kStaticTable[i].value == value)
            return i + 1;
    }
    for (u32 i = 0; i < dynamic_table_.size(); i++) {
        if (dynamic_table_[i].name == name && dynamic_table_[i].value == value)
            return 62 + i;
    }
    return 0;
}

void HPack::evict_to_fit(u32 new_entry_size) {
    u32 max_size = max_table_size_;
    while (!dynamic_table_.empty() && current_table_size_ + new_entry_size > max_size) {
        auto& last = dynamic_table_.back();
        u32 entry_size = static_cast<u32>(last.name.size() + last.value.size() + 32);
        current_table_size_ -= entry_size < current_table_size_ ? entry_size : current_table_size_;
        dynamic_table_.pop_back();
    }
}

// ============================================================================
// HPack: Constructor/Destructor
// ============================================================================

HPack::HPack() = default;
HPack::~HPack() = default;
HPack::HPack(HPack&&) noexcept = default;
HPack& HPack::operator=(HPack&&) noexcept = default;

void HPack::set_max_table_size(u32 size) {
    max_table_size_ = size;
    while (current_table_size_ > max_table_size_ && !dynamic_table_.empty()) {
        auto& last = dynamic_table_.back();
        u32 entry_size = static_cast<u32>(last.name.size() + last.value.size() + 32);
        current_table_size_ -= entry_size < current_table_size_ ? entry_size : current_table_size_;
        dynamic_table_.pop_back();
    }
}

// ============================================================================
// HPack: Decode
// ============================================================================

std::vector<HPackEntry> HPack::decode(const u8* data, u32 len) {
    std::vector<HPackEntry> entries;
    u32 pos = 0;
    while (pos < len) {
        u8 first = data[pos];
        if (first & 0x80) {
            // Indexed Header Field (1xxxxxxx)
            u32 idx = decode_integer(data, len, pos, 7);
            if (idx == 0) break;
            auto* entry = get_entry(idx);
            if (!entry) break;
            entries.push_back(*entry);
        } else if ((first & 0xC0) == 0x40) {
            // Literal with Incremental Indexing (01xxxxxx)
            u32 name_idx = decode_integer(data, len, pos, 6);
            std::string name, value;
            if (name_idx > 0) {
                auto* entry = get_entry(name_idx);
                if (!entry) break;
                name = entry->name;
            } else {
                name = decode_string(data, len, pos);
            }
            value = decode_string(data, len, pos);
            entries.push_back({name, value});
            // Add to dynamic table
            u32 entry_size = static_cast<u32>(name.size() + value.size() + 32);
            evict_to_fit(entry_size);
            if (current_table_size_ + entry_size <= max_table_size_) {
                dynamic_table_.insert(dynamic_table_.begin(), {name, value});
                current_table_size_ += entry_size;
            }
        } else if ((first & 0xF0) == 0x00) {
            // Literal without Indexing (0000xxxx)
            u32 name_idx = decode_integer(data, len, pos, 4);
            std::string name, value;
            if (name_idx > 0) {
                auto* entry = get_entry(name_idx);
                if (!entry) break;
                name = entry->name;
            } else {
                name = decode_string(data, len, pos);
            }
            value = decode_string(data, len, pos);
            entries.push_back({name, value});
        } else if ((first & 0xF0) == 0x10) {
            // Literal Never Indexed (0001xxxx)
            u32 name_idx = decode_integer(data, len, pos, 4);
            std::string name, value;
            if (name_idx > 0) {
                auto* entry = get_entry(name_idx);
                if (!entry) break;
                name = entry->name;
            } else {
                name = decode_string(data, len, pos);
            }
            value = decode_string(data, len, pos);
            entries.push_back({name, value});
        } else if ((first & 0xE0) == 0x20) {
            // Dynamic Table Size Update (001xxxxx)
            u32 new_size = decode_integer(data, len, pos, 5);
            set_max_table_size(new_size);
        } else {
            break;
        }
    }
    return entries;
}

// ============================================================================
// HPack: Encode
// ============================================================================

std::vector<u8> HPack::encode(const std::vector<HPackEntry>& headers) {
    std::vector<u8> out;
    for (auto& h : headers) {
        u32 table_idx = find_in_table(h.name, h.value);
        if (table_idx > 0) {
            // Indexed Header Field
            auto idx_enc = encode_integer(table_idx, 7);
            out.push_back(idx_enc[0] | 0x80);
            for (std::size_t i = 1; i < idx_enc.size(); i++)
                out.push_back(idx_enc[i]);
        } else {
            // Literal with Incremental Indexing
            u32 name_idx = find_name_in_table(h.name);
            if (name_idx > 0) {
                auto idx_enc = encode_integer(name_idx, 6);
                out.push_back(idx_enc[0] | 0x40);
                for (std::size_t i = 1; i < idx_enc.size(); i++)
                    out.push_back(idx_enc[i]);
            } else {
                auto idx_enc = encode_integer(0, 6);
                out.push_back(idx_enc[0] | 0x40);
                for (std::size_t i = 1; i < idx_enc.size(); i++)
                    out.push_back(idx_enc[i]);
                auto name_enc = encode_string(h.name);
                out.insert(out.end(), name_enc.begin(), name_enc.end());
            }
            auto val_enc = encode_string(h.value);
            out.insert(out.end(), val_enc.begin(), val_enc.end());
            // Add to dynamic table
            u32 entry_size = static_cast<u32>(h.name.size() + h.value.size() + 32);
            evict_to_fit(entry_size);
            if (current_table_size_ + entry_size <= max_table_size_) {
                dynamic_table_.insert(dynamic_table_.begin(), h);
                current_table_size_ += entry_size;
            }
        }
    }
    return out;
}

// ============================================================================
// Helper to get HTTP reason phrase by code
// ============================================================================

static const char* http_status_reason(u16 code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "";
    }
}

// ============================================================================
// HTTP2Client: Convert http::Request to HPack entries
// ============================================================================

std::vector<HPackEntry> HTTP2Client::request_to_hpack(const http::Request& req) {
    std::vector<HPackEntry> entries;

    // Pseudo-headers
    std::string method_str;
    switch (req.method) {
        case http::Method::GET:     method_str = "GET"; break;
        case http::Method::POST:    method_str = "POST"; break;
        case http::Method::HEAD:    method_str = "HEAD"; break;
        case http::Method::PUT:     method_str = "PUT"; break;
        case http::Method::DELETE:  method_str = "DELETE"; break;
        case http::Method::CONNECT: method_str = "CONNECT"; break;
        case http::Method::OPTIONS: method_str = "OPTIONS"; break;
        case http::Method::PATCH:   method_str = "PATCH"; break;
    }
    entries.push_back({":method", method_str});
    entries.push_back({":scheme", req.url.scheme});

    std::string authority = req.url.host;
    if (!authority.empty() && authority.front() == '[' && authority.back() == ']')
        authority = authority.substr(1, authority.size() - 2);
    if (req.url.port != 0 && req.url.port != req.url.default_port())
        authority += ":" + std::to_string(req.url.port);
    entries.push_back({":authority", authority});

    std::string path = req.url.path.empty() ? "/" : req.url.path;
    if (!req.url.query.empty())
        path += "?" + req.url.query;
    entries.push_back({":path", path});

    for (auto& [k, v] : req.headers.all()) {
        entries.push_back({k, v});
    }
    return entries;
}

Result<http::Response> HTTP2Client::hpack_to_response(const std::vector<HPackEntry>& entries) {
    http::Response resp;
    http::Headers headers;
    for (auto& e : entries) {
        if (e.name == ":status") {
            char* end = nullptr;
            long code = std::strtol(e.value.c_str(), &end, 10);
            if (*end != '\0' || code < 100 || code > 599)
                return std::string("bad :status in HTTP/2 response");
            resp.status.code = static_cast<u16>(code);
            resp.status.reason = http_status_reason(static_cast<u16>(code));
        } else if (!e.name.empty() && e.name[0] == ':') {
            // Skip other pseudo-headers
        } else {
            headers.set(e.name, e.value);
        }
    }
    resp.headers = std::move(headers);
    resp.http_version = "HTTP/2";
    if (resp.status.code == 0)
        return std::string("missing :status pseudo-header");
    return resp;
}

// ============================================================================
// HTTP2Client: Constructor/Destructor
// ============================================================================

HTTP2Client::HTTP2Client() = default;
HTTP2Client::~HTTP2Client() = default;
HTTP2Client::HTTP2Client(HTTP2Client&&) noexcept = default;
HTTP2Client& HTTP2Client::operator=(HTTP2Client&&) noexcept = default;

bool HTTP2Client::is_connected() const {
    if (!tcp_.is_open()) return false;
    if (use_tls_ && (!tls_ || !tls_->is_connected())) return false;
    return connected_;
}

void HTTP2Client::close() {
    connected_ = false;
    if (tls_) { tls_->close(); tls_.reset(); }
    tcp_.close();
    next_stream_id_ = 1;
    server_window_ = 65535;
    client_window_ = 65535;
    server_max_frame_size_ = 16384;
    server_header_table_size_ = 4096;
    server_initial_window_size_ = 65535;
    server_enable_push_ = true;
    hpack_ = HPack();
}

// ============================================================================
// HTTP2Client: Frame I/O
// ============================================================================

Result<void> HTTP2Client::send_frame(FrameType type, u8 flags, u32 stream_id,
                                      const std::vector<u8>& payload) {
    FrameHeader hdr;
    hdr.length = static_cast<u32>(payload.size());
    hdr.type = type;
    hdr.flags = flags;
    hdr.stream_id = stream_id;
    auto frame = serialize_frame(hdr, payload.data());
    if (use_tls_) {
        return tls_->send_all(frame.data(), static_cast<u32>(frame.size()));
    } else {
        return tcp_.send_all(frame.data(), static_cast<u32>(frame.size()));
    }
}

Result<u32> HTTP2Client::read_some(u8* buf, u32 len) {
    if (use_tls_) return tls_->receive(buf, len);
    return tcp_.receive(buf, len);
}

Result<FrameHeader> HTTP2Client::read_frame() {
    u8 header[9];
    u32 got = 0;
    while (got < 9) {
        auto r = read_some(header + got, 9 - got);
        if (r.is_err()) return std::string("read frame header: " + r.unwrap_err());
        u32 n = r.unwrap();
        if (n == 0) return std::string("connection closed during frame header");
        got += n;
    }
    u32 pos = 0;
    return parse_frame_header(header, 9, pos);
}

Result<u32> HTTP2Client::read_frame_payload(const FrameHeader& hdr, std::vector<u8>& out) {
    if (hdr.length == 0) {
        out.clear();
        return 0u;
    }
    out.resize(hdr.length);
    u32 got = 0;
    while (got < hdr.length) {
        auto r = read_some(out.data() + got, hdr.length - got);
        if (r.is_err()) return std::string("read frame payload: " + r.unwrap_err());
        u32 n = r.unwrap();
        if (n == 0) return std::string("connection closed during frame payload");
        got += n;
    }
    return got;
}

// ============================================================================
// HTTP2Client: Protocol helpers
// ============================================================================

Result<void> HTTP2Client::send_settings(const std::vector<std::pair<u16, u32>>& settings) {
    std::vector<u8> payload;
    for (auto& s : settings) {
        write_u16_be(payload, s.first);
        write_u32_be(payload, s.second);
    }
    return send_frame(SETTINGS, 0, 0, payload);
}

Result<void> HTTP2Client::send_window_update(u32 stream_id, u32 increment) {
    std::vector<u8> payload(4);
    // RFC 7540 §6.9: reserved bit (MSB) MUST be 0
    payload[0] = static_cast<u8>((increment >> 24) & 0x7F);
    payload[1] = static_cast<u8>((increment >> 16) & 0xFF);
    payload[2] = static_cast<u8>((increment >> 8) & 0xFF);
    payload[3] = static_cast<u8>(increment & 0xFF);
    return send_frame(WINDOW_UPDATE, 0, stream_id, payload);
}

Result<void> HTTP2Client::send_goaway(u32 last_stream_id, u32 error_code) {
    std::vector<u8> payload(8);
    // RFC 7540 §6.8: reserved bit (MSB) MUST be 0
    payload[0] = static_cast<u8>((last_stream_id >> 24) & 0x7F);
    payload[1] = static_cast<u8>((last_stream_id >> 16) & 0xFF);
    payload[2] = static_cast<u8>((last_stream_id >> 8) & 0xFF);
    payload[3] = static_cast<u8>(last_stream_id & 0xFF);
    payload[4] = static_cast<u8>((error_code >> 24) & 0xFF);
    payload[5] = static_cast<u8>((error_code >> 16) & 0xFF);
    payload[6] = static_cast<u8>((error_code >> 8) & 0xFF);
    payload[7] = static_cast<u8>(error_code & 0xFF);
    return send_frame(GOAWAY, 0, 0, payload);
}

Result<void> HTTP2Client::send_preface() {
    // Connection preface: PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n
    static const u8 kPreface[24] = {
        0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32,
        0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a
    };
    if (use_tls_) {
        auto r = tls_->send_all(kPreface, 24);
        if (r.is_err()) return std::string("send preface: " + r.unwrap_err());
    } else {
        auto r = tcp_.send_all(kPreface, 24);
        if (r.is_err()) return std::string("send preface: " + r.unwrap_err());
    }
    // Send empty SETTINGS
    return send_settings({});
}

Result<void> HTTP2Client::read_and_process_settings() {
    while (true) {
        auto fh_r = read_frame();
        if (fh_r.is_err()) return std::string("read settings: " + fh_r.unwrap_err());
        auto fh = fh_r.unwrap();

        if (fh.length > server_max_frame_size_)
            return std::string("frame exceeds max frame size");

        std::vector<u8> payload;
        auto pr = read_frame_payload(fh, payload);
        if (pr.is_err()) return std::string("read settings payload: " + pr.unwrap_err());

        if (fh.type == SETTINGS && fh.stream_id == 0) {
            if (fh.flags & 0x01) {
                continue;
            }
            u32 sp = 0;
            while (sp + 6 <= fh.length) {
                u16 id = static_cast<u16>((payload[sp] << 8) | payload[sp + 1]);
                u32 val = (static_cast<u32>(payload[sp + 2]) << 24) |
                          (static_cast<u32>(payload[sp + 3]) << 16) |
                          (static_cast<u32>(payload[sp + 4]) << 8) |
                          payload[sp + 5];
                sp += 6;
                switch (id) {
                    case SETTINGS_HEADER_TABLE_SIZE:
                        server_header_table_size_ = val;
                        hpack_.set_max_table_size(val);
                        break;
                    case SETTINGS_ENABLE_PUSH:
                        server_enable_push_ = (val != 0);
                        break;
                    case SETTINGS_MAX_CONCURRENT_STREAMS:
                        break;
                    case SETTINGS_INITIAL_WINDOW_SIZE:
                        server_window_ = server_window_ + (val - server_initial_window_size_);
                        server_initial_window_size_ = val;
                        break;
                    case SETTINGS_MAX_FRAME_SIZE:
                        if (val < 16384 || val > 16777215)
                            return std::string("invalid max frame size");
                        server_max_frame_size_ = val;
                        break;
                    case SETTINGS_MAX_HEADER_LIST_SIZE:
                        break;
                }
            }
            return send_frame(SETTINGS, 0x01, 0, {});
        } else if (fh.type == WINDOW_UPDATE && fh.stream_id == 0 && fh.length >= 4) {
            u32 increment = (static_cast<u32>(payload[0]) << 24) |
                            (static_cast<u32>(payload[1]) << 16) |
                            (static_cast<u32>(payload[2]) << 8) |
                            payload[3];
            server_window_ += increment;
        } else if (fh.type == GOAWAY && fh.stream_id == 0) {
            return std::string("server sent GOAWAY during settings");
        } else if (fh.type == PING && (fh.flags & 0x01) == 0) {
            auto sr = send_frame(PING, 0x01, 0, payload);
            if (sr.is_err()) return sr;
        }
    }
    return {};
}

Result<void> HTTP2Client::connect(const std::string& host, u16 port, bool use_tls,
                                   Connection* existing_tcp,
                                   tls::TLSConnection* existing_tls) {
    close();
    use_tls_ = use_tls;

    if (existing_tcp && existing_tls) {
        tcp_ = std::move(*existing_tcp);
        tls_.reset(existing_tls);
    } else {
        ConnectionConfig cfg;
        cfg.connect_timeout_ms = 10000;
        cfg.read_timeout_ms = 30000;
        auto r = tcp_.open(host, port, cfg);
        if (r.is_err()) return std::string("connect: " + r.unwrap_err());

        if (use_tls_) {
            tls_ = std::make_unique<tls::TLSConnection>();
            auto tr = tls_->connect(&tcp_, host);
            if (tr.is_err()) {
                close();
                return std::string("tls: " + tr.unwrap_err());
            }
            // Verify negotiated ALPN is "h2"
            if (tls_->negotiated_alpn() != "h2") {
                close();
                return std::string("server does not support h2, got: " + tls_->negotiated_alpn());
            }
        }
    }

    // Send connection preface
    auto pr = send_preface();
    if (pr.is_err()) { close(); return pr; }

    // Read server SETTINGS and send ACK
    auto sr = read_and_process_settings();
    if (sr.is_err()) { close(); return sr; }

    connected_ = true;
    return {};
}

Result<http::Response> HTTP2Client::execute(const http::Request& req) {
    if (!connected_) return std::string("not connected");

    u32 stream_id = next_stream_id_;
    next_stream_id_ += 2;

    // Build HPack-encoded headers
    auto entries = request_to_hpack(req);
    // We need to include request headers from req.headers — but Headers doesn't expose iteration.
    // We handle this by adding an entries()/get_all() method. For now, we read headers
    // from the Request's body/headers by serializing and reparsing...
    // Actually, let's use a hack for now: serialize the Request headers to string, parse them.
    // Better approach: we can get headers from the serialized form.

    auto hpack_data = hpack_.encode(entries);

    // Send HEADERS frame
    u8 flags = 0x04; // END_HEADERS
    if (req.body.empty()) flags |= 0x01; // END_STREAM
    auto sr = send_frame(HEADERS, flags, stream_id, hpack_data);
    if (sr.is_err()) return std::string("send headers: " + sr.unwrap_err());

    // Read response
    std::vector<HPackEntry> resp_entries;
    bool headers_complete = false;
    bool has_body = false;
    std::vector<u8> body;

    while (true) {
        auto fh_r = read_frame();
        if (fh_r.is_err()) { close(); return std::string("read frame: " + fh_r.unwrap_err()); }
        auto fh = fh_r.unwrap();

        if (fh.length > server_max_frame_size_) {
            close();
            return std::string("frame exceeds max frame size");
        }

        std::vector<u8> payload;
        auto pr = read_frame_payload(fh, payload);
        if (pr.is_err()) { close(); return std::string("read payload: " + pr.unwrap_err()); }

        if (fh.stream_id == 0) {
            // Connection-level frame
            if (fh.type == GOAWAY) {
                u32 last_stream = 0;
                u32 err_code = 0;
                if (fh.length >= 8) {
                    last_stream = (static_cast<u32>(payload[0]) << 24) |
                                  (static_cast<u32>(payload[1]) << 16) |
                                  (static_cast<u32>(payload[2]) << 8) |
                                  payload[3];
                    err_code = (static_cast<u32>(payload[4]) << 24) |
                               (static_cast<u32>(payload[5]) << 16) |
                               (static_cast<u32>(payload[6]) << 8) |
                               payload[7];
                } else if (fh.length >= 4) {
                    last_stream = (static_cast<u32>(payload[0]) << 24) |
                                  (static_cast<u32>(payload[1]) << 16) |
                                  (static_cast<u32>(payload[2]) << 8) |
                                  payload[3];
                }
                return std::string("GOAWAY: stream=" + std::to_string(last_stream) +
                                   " err=" + std::to_string(err_code));
            }
            if (fh.type == PING && (fh.flags & 0x01) == 0) {
                auto sr = send_frame(PING, 0x01, 0, payload);
                if (sr.is_err()) { close(); return std::string("ping ack: " + sr.unwrap_err()); }
            }
            if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                u32 inc = (static_cast<u32>(payload[0]) << 24) |
                          (static_cast<u32>(payload[1]) << 16) |
                          (static_cast<u32>(payload[2]) << 8) |
                          payload[3];
                server_window_ += inc;
            }
            continue;
        }

        if (fh.stream_id != stream_id) {
            continue;
        }

        if (fh.type == HEADERS || fh.type == CONTINUATION) {
            auto new_entries = hpack_.decode(payload.data(), fh.length);
            resp_entries.insert(resp_entries.end(), new_entries.begin(), new_entries.end());
            if (fh.flags & 0x04) {
                headers_complete = true;
            }
            if (fh.flags & 0x01) {
                has_body = false;
                break;
            }
        } else if (fh.type == DATA) {
            if (!headers_complete) {
                close();
                return std::string("DATA before HEADERS complete");
            }
            if (fh.length > server_window_) {
                close();
                return std::string("flow control window exceeded");
            }
            has_body = true;
            body.insert(body.end(), payload.begin(), payload.begin() + fh.length);
            server_window_ -= fh.length;
            auto sr1 = send_window_update(stream_id, fh.length);
            auto sr2 = send_window_update(0, fh.length);
            if (sr1.is_err() || sr2.is_err()) { close(); return std::string("window update failed"); }
            if (fh.flags & 0x01) {
                break;
            }
        } else if (fh.type == RST_STREAM) {
            u32 err_code = 0;
            if (fh.length >= 4) {
                err_code = (static_cast<u32>(payload[0]) << 24) |
                           (static_cast<u32>(payload[1]) << 16) |
                           (static_cast<u32>(payload[2]) << 8) |
                           payload[3];
            }
            return std::string("RST_STREAM: err=" + std::to_string(err_code));
        } else if (fh.type == GOAWAY) {
            return std::string("server sent GOAWAY");
        } else if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
            u32 inc = (static_cast<u32>(payload[0]) << 24) |
                      (static_cast<u32>(payload[1]) << 16) |
                      (static_cast<u32>(payload[2]) << 8) |
                      payload[3];
            server_window_ += inc;
        }
    }

    if (!headers_complete) {
        close();
        return std::string("incomplete headers");
    }

    // Parse response
    auto resp_r = hpack_to_response(resp_entries);
    if (resp_r.is_err()) return resp_r;
    auto resp = resp_r.unwrap();

    if (has_body) {
        // Check transfer-encoding / content-length
        if (resp.headers.has("content-length")) {
            std::string cl = resp.headers.get("content-length");
            char* cl_end = nullptr;
            long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
            if (*cl_end == '\0' && cl_val >= 0) {
                if (body.size() > static_cast<std::size_t>(cl_val))
                    body.resize(static_cast<std::size_t>(cl_val));
            }
        }
        resp.body = std::move(body);
    }

    return resp;
}

} // namespace browser::net::http2
