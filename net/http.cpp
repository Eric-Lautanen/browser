#include "http.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace browser::net::http {

// --- Headers ---

void Headers::set(const std::string& key, const std::string& value) {
    headers_[normalize_key(key)] = value;
}

std::string Headers::get(const std::string& key) const {
    auto it = headers_.find(normalize_key(key));
    return it != headers_.end() ? it->second : std::string();
}

bool Headers::has(const std::string& key) const {
    return headers_.find(normalize_key(key)) != headers_.end();
}

void Headers::remove(const std::string& key) {
    headers_.erase(normalize_key(key));
}

std::string Headers::to_string() const {
    std::string result;
    for (auto& [k, v] : headers_) {
        result += k + ": " + v + "\r\n";
    }
    return result;
}

Result<Headers> Headers::parse(const u8* data, u32 len, u32& consumed) {
    Headers h;
    u32 pos = 0;
    while (pos + 2 <= len) {
        // Check for end of headers (empty line: \r\n)
        if (pos + 1 < len && data[pos] == '\r' && data[pos + 1] == '\n') {
            consumed = pos + 2;
            return h;
        }

        // Read header line
        std::string line;
        while (pos < len) {
            if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') {
                pos += 2;
                break;
            }
            line += static_cast<char>(data[pos]);
            pos++;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos) {
            consumed = pos;
            return h;
        }

        std::string name = line.substr(0, colon);
        std::string value;
        if (colon + 1 < line.size() && line[colon + 1] == ' ')
            value = line.substr(colon + 2);
        else
            value = line.substr(colon + 1);

        // Trim leading/trailing whitespace
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();

        h.set(name, value);
    }
    consumed = pos;
    return h;
}

// --- Request ---

std::vector<u8> Request::serialize() const {
    static const char* method_names[] = {
        "GET", "POST", "HEAD", "PUT", "DELETE", "CONNECT", "OPTIONS", "PATCH"
    };
    int mi = static_cast<int>(method);
    const char* mn = (mi >= 0 && mi < 8) ? method_names[mi] : "GET";

    std::string req_line = std::string(mn) + " " +
                           (url.path.empty() ? "/" : url.path) +
                           (url.query.empty() ? "" : "?" + url.query) +
                           " HTTP/1.1\r\n";

    Headers hdrs = headers;
    // Auto-insert Host header if not present (required by HTTP/1.1)
    if (!hdrs.has("host")) {
        std::string host = url.host;
        // Strip IPv6 brackets for Host header
        if (!host.empty() && host.front() == '[' && host.back() == ']')
            host = host.substr(1, host.size() - 2);
        if (url.port != 0 && url.port != url.default_port())
            host += ":" + std::to_string(url.port);
        hdrs.set("Host", host);
    }

    std::string header_str = req_line + hdrs.to_string();
    header_str += "\r\n";

    std::vector<u8> result(header_str.begin(), header_str.end());
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

// --- Response ---

Result<Response> Response::parse(const u8* data, u32 len) {
    Response resp;
    u32 pos = 0;

    // Status line: HTTP/x.x SP status_code SP reason CRLF
    std::string status_line;
    while (pos < len) {
        if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') {
            pos += 2;
            break;
        }
        status_line += static_cast<char>(data[pos]);
        pos++;
    }
    if (status_line.empty()) return std::string("empty status line");

    // Parse "HTTP/x.x" version
    auto sp1 = status_line.find(' ');
    if (sp1 == std::string::npos) return std::string("bad status line");
    resp.http_version = status_line.substr(0, sp1);

    // Parse status code
    auto sp2 = status_line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return std::string("bad status line: missing reason");
    std::string code_str = status_line.substr(sp1 + 1, sp2 - sp1 - 1);
    char* end = nullptr;
    long code = std::strtol(code_str.c_str(), &end, 10);
    if (*end != '\0' || code < 100 || code > 599)
        return std::string("bad status code");
    resp.status.code = static_cast<u16>(code);

    // Parse reason
    resp.status.reason = status_line.substr(sp2 + 1);

    // Parse headers
    u32 hdrs_consumed = 0;
    auto hdrs_r = Headers::parse(data + pos, len - pos, hdrs_consumed);
    if (hdrs_r.is_err()) return std::string("bad headers");
    resp.headers = std::move(hdrs_r.unwrap());
    pos += hdrs_consumed;

    // Parse body based on transfer-encoding and content-length
    bool chunked = resp.headers.has("transfer-encoding") &&
                   resp.headers.get("transfer-encoding").find("chunked") != std::string::npos;

    if (chunked) {
        // Chunked transfer encoding
        while (pos < len) {
            // Read chunk size line
            std::string size_line;
            while (pos < len) {
                if (data[pos] == '\r' && pos + 1 < len && data[pos + 1] == '\n') {
                    pos += 2;
                    break;
                }
                size_line += static_cast<char>(data[pos]);
                pos++;
            }
            if (size_line.empty()) break;

            // Parse hex chunk size (handle chunk extensions)
            std::string hex_size;
            for (char c : size_line) {
                if (c == ';') break;
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                    hex_size += c;
            }
            if (hex_size.empty()) break;

            u32 chunk_size = 0;
            for (char c : hex_size) {
                chunk_size *= 16;
                if (c >= '0' && c <= '9') chunk_size += static_cast<u32>(c - '0');
                else if (c >= 'a' && c <= 'f') chunk_size += static_cast<u32>(c - 'a' + 10);
                else chunk_size += static_cast<u32>(c - 'A' + 10);
            }

            if (chunk_size == 0) {
                // Skip trailing CRLF after last chunk
                if (pos + 1 < len && data[pos] == '\r' && data[pos + 1] == '\n')
                    pos += 2;
                break;
            }

            if (pos + chunk_size > len) {
                // Partial chunk — store what we have
                resp.body.insert(resp.body.end(), data + pos, data + len);
                pos = len;
                break;
            }

            resp.body.insert(resp.body.end(), data + pos, data + pos + chunk_size);
            pos += chunk_size;

            // Skip trailing CRLF
            if (pos + 1 < len && data[pos] == '\r' && data[pos + 1] == '\n')
                pos += 2;
        }
    } else if (resp.headers.has("content-length")) {
        std::string cl = resp.headers.get("content-length");
        char* cl_end = nullptr;
        long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
        if (*cl_end != '\0' || cl_val < 0) return std::string("bad content-length");
        u32 remaining = static_cast<u32>(cl_val);
        u32 available = (pos < len) ? len - pos : 0;
        u32 to_copy = remaining < available ? remaining : available;
        resp.body.assign(data + pos, data + pos + to_copy);
        pos += to_copy;
    } else if (resp.status.code >= 200 && resp.status.code != 204 && resp.status.code != 304) {
        // No content-length, no chunked — read until connection close
        if (pos < len) {
            resp.body.assign(data + pos, data + len);
        }
    }

    return resp;
}

// --- HTTP1Client ---

HTTP1Client::HTTP1Client() = default;
HTTP1Client::~HTTP1Client() = default;
HTTP1Client::HTTP1Client(HTTP1Client&&) noexcept = default;
HTTP1Client& HTTP1Client::operator=(HTTP1Client&&) noexcept = default;

Result<void> HTTP1Client::connect(const std::string& host, u16 port, bool use_tls,
                                   Connection* existing_tcp,
                                   tls::TLSConnection* existing_tls) {
    close();
    use_tls_ = use_tls;
    if (existing_tcp && existing_tls) {
        tcp_ = std::move(*existing_tcp);
        tls_.reset(existing_tls);
    } else if (existing_tcp) {
        tcp_ = std::move(*existing_tcp);
    } else {
        ConnectionConfig cfg;
        cfg.connect_timeout_ms = 10000;
        cfg.read_timeout_ms = 30000;
        auto r = tcp_.open(host, port, cfg);
        if (r.is_err()) return std::string("connect: " + r.unwrap_err());
        if (use_tls_) {
            tls_ = std::make_unique<tls::TLSConnection>();
            auto tr = tls_->connect(&tcp_, host);
            if (tr.is_err()) { close(); return std::string("tls: " + tr.unwrap_err()); }
        }
    }
    return {};
}

Result<void> HTTP1Client::connect_if_needed(const Request& req) {
    use_tls_ = (req.url.scheme == "https");
    std::string host = req.url.host;
    // Strip IPv6 brackets for host header
    if (!host.empty() && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    u16 port = req.url.port != 0 ? req.url.port : req.url.default_port();

    if (tcp_.is_open() && tcp_.host() == host && tcp_.port() == port) {
        if (use_tls_ && tls_ && tls_->is_connected()) return {};
        if (!use_tls_) return {};
    }

    close();

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
    }

    return {};
}

Result<Response> HTTP1Client::read_response() {
    std::vector<u8> all_data;

    // Read until we have complete headers
    auto try_parse = [&]() -> Result<Response> {
        return Response::parse(all_data.data(), static_cast<u32>(all_data.size()));
    };

    if (use_tls_) {
        while (true) {
            auto r = tls_->receive_all(65536);
            if (r.is_err()) {
                if (all_data.empty()) return std::string("receive: " + r.unwrap_err());
                break;
            }
            auto& chunk = r.unwrap();
            if (chunk.empty()) break;
            all_data.insert(all_data.end(), chunk.begin(), chunk.end());

            auto parsed = try_parse();
            if (parsed.is_ok()) return parsed;
            if (all_data.size() > 1024 * 1024) break;
        }
    } else {
        auto r = tcp_.receive_until_close(65536);
        if (r.is_err()) return std::string("receive: " + r.unwrap_err());
        all_data = std::move(r.unwrap());
    }

    return try_parse();
}

Result<Response> HTTP1Client::execute(const Request& req) {
    auto cr = connect_if_needed(req);
    if (cr.is_err()) return std::string("execute: " + cr.unwrap_err());

    auto wire = req.serialize();

    if (use_tls_) {
        auto sr = tls_->send_all(wire.data(), static_cast<u32>(wire.size()));
        if (sr.is_err()) return std::string("send: " + sr.unwrap_err());
    } else {
        auto sr = tcp_.send_all(wire.data(), static_cast<u32>(wire.size()));
        if (sr.is_err()) return std::string("send: " + sr.unwrap_err());
    }

    return read_response();
}

async::task<Response> HTTP1Client::execute_async(const Request& req) {
    auto cr = connect_if_needed(req);
    if (cr.is_err()) co_return std::string("execute: ") + cr.unwrap_err();

    auto wire = req.serialize();

    if (use_tls_) {
        auto sr = co_await tls_->send_all_async(wire.data(), static_cast<u32>(wire.size()));
        if (sr.is_err()) co_return std::string("send: ") + sr.unwrap_err();
    } else {
        auto sr = co_await tcp_.send_all_async(wire.data(), static_cast<u32>(wire.size()));
        if (sr.is_err()) co_return std::string("send: ") + sr.unwrap_err();
    }

    // Read response using async methods
    std::vector<u8> all_data;
    auto try_parse = [&]() -> Result<Response> {
        return Response::parse(all_data.data(), static_cast<u32>(all_data.size()));
    };

    if (use_tls_) {
        while (true) {
            u8 buf[65536];
            auto r = co_await tls_->receive_async(buf, sizeof(buf));
            if (r.is_err()) {
                if (all_data.empty()) co_return std::string("receive: ") + r.unwrap_err();
                break;
            }
            u32 n = r.unwrap();
            if (n == 0) break;
            all_data.insert(all_data.end(), buf, buf + n);

            auto parsed = try_parse();
            if (parsed.is_ok()) co_return parsed.unwrap();
            if (all_data.size() > 1024 * 1024) break;
        }
    } else {
        auto r = co_await tcp_.receive_until_close_async(65536);
        if (r.is_err()) co_return std::string("receive: ") + r.unwrap_err();
        auto vec = r.unwrap();
        all_data = std::move(vec);
    }

    co_return try_parse();
}

void HTTP1Client::close() {
    if (tls_) { tls_->close(); tls_.reset(); }
    tcp_.close();
}

bool HTTP1Client::is_connected() const {
    if (!tcp_.is_open()) return false;
    if (use_tls_ && (!tls_ || !tls_->is_connected())) return false;
    return true;
}

} // namespace browser::net::http
