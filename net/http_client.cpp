#include "http_client.hpp"
#include "origin.hpp"
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <chrono>

namespace browser::net {

TrackerBlocker* HTTPClient::tracker_ = nullptr;

HTTPClient::HTTPClient() = default;
HTTPClient::~HTTPClient() = default;
HTTPClient::HTTPClient(HTTPClient&&) noexcept = default;
HTTPClient& HTTPClient::operator=(HTTPClient&&) noexcept = default;

static std::string trim_str(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    return s.substr(start, end - start);
}

static void parse_set_cookie(const std::string& header_value,
                             const std::string& request_domain,
                             const std::string& request_path,
                             CookieJar& jar) {
    std::string trimmed = trim_str(header_value);
    if (trimmed.empty()) return;

    // Split on semicolon for attributes
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < trimmed.size()) {
        size_t next = trimmed.find(';', pos);
        if (next == std::string::npos) next = trimmed.size();
        parts.push_back(trim_str(trimmed.substr(pos, next - pos)));
        pos = next + 1;
    }

    if (parts.empty()) return;

    // First part: name=value
    auto eq = parts[0].find('=');
    if (eq == std::string::npos) return;

    Cookie c;
    c.name = trim_str(parts[0].substr(0, eq));
    c.value = trim_str(parts[0].substr(eq + 1));
    c.creation_time = static_cast<u64>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    c.last_access_time = c.creation_time;
    c.path = "/";

    for (size_t i = 1; i < parts.size(); i++) {
        auto attr_eq = parts[i].find('=');
        std::string attr_name, attr_value;
        if (attr_eq != std::string::npos) {
            attr_name = trim_str(parts[i].substr(0, attr_eq));
            attr_value = trim_str(parts[i].substr(attr_eq + 1));
        } else {
            attr_name = trim_str(parts[i]);
        }

        std::string lc_name;
        for (char ch : attr_name) lc_name += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (lc_name == "domain") {
            if (!attr_value.empty() && attr_value.front() == '.')
                c.domain = attr_value;
            else
                c.domain = "." + attr_value;
        } else if (lc_name == "path") {
            if (!attr_value.empty() && attr_value.front() == '/')
                c.path = attr_value;
        } else if (lc_name == "secure") {
            c.secure = true;
        } else if (lc_name == "httponly") {
            c.httpOnly = true;
        } else if (lc_name == "max-age") {
            char* end = nullptr;
            long max_age = std::strtol(attr_value.c_str(), &end, 10);
            if (end != attr_value.c_str()) {
                if (max_age <= 0) return;
                c.expires_time = c.creation_time + static_cast<u64>(max_age);
            }
        } else if (lc_name == "expires") {
            // Parse HTTP date - simplified: just skip for now, max-age preferred
        } else if (lc_name == "samesite") {
            std::string lc_val;
            for (char ch : attr_value) lc_val += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (lc_val == "lax" || lc_val == "strict" || lc_val == "none")
                c.sameSite = lc_val;
        }
    }

    if (c.domain.empty()) {
        c.domain = request_domain;
    } else {
        std::string req_lower;
        for (char ch : request_domain) req_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::string cd_lower;
        for (char ch : c.domain) cd_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        // Strip leading dot for validation
        std::string check_domain = cd_lower;
        if (!check_domain.empty() && check_domain.front() == '.')
            check_domain = check_domain.substr(1);

        // TLD check: domain must contain a dot (unless localhost)
        if (check_domain.find('.') == std::string::npos && check_domain != "localhost")
            return;

        // Domain must equal request domain or be a suffix
        if (req_lower != check_domain) {
            std::string dotted = "." + check_domain;
            if (!(req_lower.size() > dotted.size() &&
                  req_lower.substr(req_lower.size() - dotted.size()) == dotted))
                return;
        }
    }

    jar.set_cookie(request_domain, request_path, c);
}

bool HTTPClient::is_connected() const {
    if (http1_ && http1_->is_connected()) return true;
    if (http2_ && http2_->is_connected()) return true;
    return false;
}

void HTTPClient::close() {
    if (http1_) { http1_->close(); http1_.reset(); }
    if (http2_) { http2_->close(); http2_.reset(); }
    if (tls_) { tls_->close(); tls_.reset(); }
    tcp_.close();
}

Result<void> HTTPClient::connect_if_needed(const http::Request& req) {
    if (tracker_ && tracker_->should_block(req.url.to_string())) {
        return std::string("blocked by tracker blocker");
    }

    use_tls_ = (req.url.scheme == "https");
    std::string host = req.url.host;
    if (!host.empty() && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    u16 port = req.url.port != 0 ? req.url.port : req.url.default_port();

    if (tcp_.is_open() && tcp_.host() == host && tcp_.port() == port) {
        if (http1_ && http1_->is_connected()) return {};
        if (http2_ && http2_->is_connected()) return {};
    }

    close();

    ConnectionConfig cfg;
    cfg.connect_timeout_ms = 3000;
    cfg.read_timeout_ms = 5000;
    auto r = tcp_.open(host, port, cfg);
    if (r.is_err()) return std::string("connect: " + r.unwrap_err());

    if (use_tls_) {
        tls_ = std::make_unique<tls::TLSConnection>();
        auto tr = tls_->connect(&tcp_, host);
        if (tr.is_err()) { close(); return std::string("tls: " + tr.unwrap_err()); }

        std::string alpn = tls_->negotiated_alpn();
        tls::TLSConnection* raw_tls = tls_.release();
        if (alpn == "h2") {
            http2_ = std::make_unique<http2::HTTP2Client>();
            auto h2r = http2_->connect(host, port, true, &tcp_, raw_tls);
            if (h2r.is_err()) { delete raw_tls; close(); return std::string("h2: ") + h2r.unwrap_err(); }
        } else {
            http1_ = std::make_unique<http::HTTP1Client>();
            auto h1r = http1_->connect(host, port, true, &tcp_, raw_tls);
            if (h1r.is_err()) { delete raw_tls; close(); return std::string("h1: ") + h1r.unwrap_err(); }
        }
    } else {
        http1_ = std::make_unique<http::HTTP1Client>();
        auto h1r = http1_->connect(host, port, false, &tcp_, nullptr);
        if (h1r.is_err()) { close(); return std::string("h1: ") + h1r.unwrap_err(); }
    }

    return {};
}

async::task<bool> HTTPClient::connect_if_needed_async(const http::Request& req) {
    if (tracker_ && tracker_->should_block(req.url.to_string())) {
        co_return std::string("blocked by tracker blocker");
    }

    use_tls_ = (req.url.scheme == "https");
    std::string host = req.url.host;
    if (!host.empty() && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    u16 port = req.url.port != 0 ? req.url.port : req.url.default_port();

    if (tcp_.is_open() && tcp_.host() == host && tcp_.port() == port) {
        if (http1_ && http1_->is_connected()) co_return true;
        if (http2_ && http2_->is_connected()) co_return true;
    }

    close();

    ConnectionConfig cfg;
    cfg.connect_timeout_ms = 3000;
    cfg.read_timeout_ms = 5000;
    auto open_r = co_await tcp_.open_async(host, port, cfg);
    if (open_r.is_err()) co_return std::string("connect: ") + open_r.unwrap_err();

    if (use_tls_) {
        tls_ = std::make_unique<tls::TLSConnection>();
        auto tr = co_await tls_->connect_async(&tcp_, host);
        if (tr.is_err()) { close(); co_return std::string("tls: ") + tr.unwrap_err(); }

        std::string alpn = tls_->negotiated_alpn();
        tls::TLSConnection* raw_tls = tls_.release();
        if (alpn == "h2") {
            http2_ = std::make_unique<http2::HTTP2Client>();
            auto h2r = http2_->connect(host, port, true, &tcp_, raw_tls);
            if (h2r.is_err()) { delete raw_tls; close(); co_return std::string("h2: ") + h2r.unwrap_err(); }
        } else {
            http1_ = std::make_unique<http::HTTP1Client>();
            auto h1r = http1_->connect(host, port, true, &tcp_, raw_tls);
            if (h1r.is_err()) { delete raw_tls; close(); co_return std::string("h1: ") + h1r.unwrap_err(); }
        }
    } else {
        http1_ = std::make_unique<http::HTTP1Client>();
        auto h1r = http1_->connect(host, port, false, &tcp_, nullptr);
        if (h1r.is_err()) { close(); co_return std::string("h1: ") + h1r.unwrap_err(); }
    }

    co_return true;
}

Result<http::Response> HTTPClient::fetch(const http::Request& req) {
    auto cr = connect_if_needed(req);
    if (cr.is_err()) return std::string("fetch: " + cr.unwrap_err());

    bool secure = (req.url.scheme == "https");

    http::Request req_with_cookies = req;
    auto matching = cookie_jar().get_cookies(req.url.host, req.url.path, secure);
    if (!matching.empty()) {
        std::string cookie_str;
        for (size_t i = 0; i < matching.size(); i++) {
            if (i > 0) cookie_str += "; ";
            cookie_str += matching[i].name + "=" + matching[i].value;
        }
        req_with_cookies.headers.set("Cookie", cookie_str);
    }

    {
        auto page_origin = Origin::from_url_str(page_url_);
        auto req_origin = Origin::from_url(req_with_cookies.url);
        bool cross_origin = !page_origin.is_same_origin(req_origin);
        bool is_post = (req_with_cookies.method == http::Method::POST);
        if (cross_origin || is_post) {
            req_with_cookies.headers.set("Origin", page_origin.to_string());
        }
    }

    if (http2_) {
        auto result = http2_->execute(req_with_cookies);
        if (result.is_ok()) {
            auto resp = std::move(result.unwrap());
            for (auto& [hk, hv] : resp.headers.all()) {
                std::string lk;
                for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lk == "set-cookie") {
                    parse_set_cookie(hv, req.url.host, req.url.path, cookie_jar());
                }
            }
            return resp;
        }
        return result;
    } else if (http1_) {
        auto result = http1_->execute(req_with_cookies);
        if (result.is_ok()) {
            auto resp = std::move(result.unwrap());
            for (auto& [hk, hv] : resp.headers.all()) {
                std::string lk;
                for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lk == "set-cookie") {
                    parse_set_cookie(hv, req.url.host, req.url.path, cookie_jar());
                }
            }
            return resp;
        }
        return result;
    }

    return std::string("no client available");
}

async::task<http::Response> HTTPClient::fetch_async(const http::Request& req) {
    auto cr = co_await connect_if_needed_async(req);
    if (cr.is_err()) co_return std::string("fetch: ") + cr.unwrap_err();

    bool secure = (req.url.scheme == "https");

    http::Request req_with_cookies = req;
    auto matching = cookie_jar().get_cookies(req.url.host, req.url.path, secure);
    if (!matching.empty()) {
        std::string cookie_str;
        for (size_t i = 0; i < matching.size(); i++) {
            if (i > 0) cookie_str += "; ";
            cookie_str += matching[i].name + "=" + matching[i].value;
        }
        req_with_cookies.headers.set("Cookie", cookie_str);
    }

    {
        auto page_origin = Origin::from_url_str(page_url_);
        auto req_origin = Origin::from_url(req_with_cookies.url);
        bool cross_origin = !page_origin.is_same_origin(req_origin);
        bool is_post = (req_with_cookies.method == http::Method::POST);
        if (cross_origin || is_post) {
            req_with_cookies.headers.set("Origin", page_origin.to_string());
        }
    }

    if (http2_) {
        auto result = http2_->execute(req_with_cookies);
        if (result.is_ok()) {
            auto resp = std::move(result.unwrap());
            for (auto& [hk, hv] : resp.headers.all()) {
                std::string lk;
                for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lk == "set-cookie") {
                    parse_set_cookie(hv, req.url.host, req.url.path, cookie_jar());
                }
            }
            co_return resp;
        }
        co_return result;
    } else if (http1_) {
        auto result = co_await http1_->execute_async(req_with_cookies);
        if (result.is_ok()) {
            auto resp = std::move(result.unwrap());
            for (auto& [hk, hv] : resp.headers.all()) {
                std::string lk;
                for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lk == "set-cookie") {
                    parse_set_cookie(hv, req.url.host, req.url.path, cookie_jar());
                }
            }
            co_return resp;
        }
        co_return result;
    }

    co_return std::string("no client available");
}

Result<http::Response> HTTPClient::get(const std::string& url_str) {
    auto url_r = URL::parse(url_str);
    if (url_r.is_err()) return std::string("bad url: " + url_r.unwrap_err());

    http::Request req;
    req.method = http::Method::GET;
    req.url = url_r.unwrap();
    {
        std::string host_hdr = req.url.host;
        if (req.url.port != 0 && req.url.port != req.url.default_port())
            host_hdr += ":" + std::to_string(req.url.port);
        req.headers.set("Host", host_hdr);
    }
    req.headers.set("Connection", "close");

    return fetch(req);
}

async::task<http::Response> HTTPClient::get_async(const std::string& url_str) {
    auto url_r = URL::parse(url_str);
    if (url_r.is_err()) co_return std::string("bad url: ") + url_r.unwrap_err();

    http::Request req;
    req.method = http::Method::GET;
    req.url = url_r.unwrap();
    {
        std::string host_hdr = req.url.host;
        if (req.url.port != 0 && req.url.port != req.url.default_port())
            host_hdr += ":" + std::to_string(req.url.port);
        req.headers.set("Host", host_hdr);
    }
    req.headers.set("Connection", "close");

    auto resp = co_await fetch_async(req);
    co_return resp;
}

} // namespace browser::net

