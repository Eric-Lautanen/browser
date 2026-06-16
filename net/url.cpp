#include "url.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace browser::net {

static bool is_scheme_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
}

static bool is_unreserved(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_' || c == '~';
}

static std::string to_lower(const std::string& s) {
    std::string r(s.size(), 0);
    for (std::size_t i = 0; i < s.size(); i++)
        r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return r;
}

std::string url_encode(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (is_unreserved(static_cast<char>(c))) {
            result += static_cast<char>(c);
        } else {
            const char hex[] = "0123456789ABCDEF";
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
        }
    }
    return result;
}

std::string url_decode(const std::string& s) {
    std::string result;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex_to_val = [](char c) -> u8 {
                if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
                if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
                if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
                return 0;
            };
            u8 v = static_cast<u8>((hex_to_val(s[i + 1]) << 4) | hex_to_val(s[i + 2]));
            result += static_cast<char>(v);
            i += 2;
        } else {
            result += s[i];
        }
    }
    return result;
}

u16 URL::default_port() const {
    if (scheme == "https") return 443;
    if (scheme == "http") return 80;
    if (scheme == "ftp") return 21;
    return 0;
}

std::string URL::to_string() const {
    std::string result = scheme + "://";
    if (!username.empty()) {
        result += username;
        if (!password.empty()) result += ':' + password;
        result += '@';
    }
    result += host;
    if (port != 0 && port != default_port()) {
        result += ':' + std::to_string(port);
    }
    if (path.empty() || path[0] != '/') result += '/';
    result += path;
    if (!query.empty()) result += '?' + query;
    if (!fragment.empty()) result += '#' + fragment;
    return result;
}

Result<URL> URL::parse(const std::string& url) {
    URL u;
    std::size_t pos = 0;
    std::size_t len = url.size();

    // Parse scheme
    std::size_t scheme_end = url.find(':');
    if (scheme_end == std::string::npos || scheme_end == 0)
        return std::string("missing scheme");
    for (std::size_t i = 0; i < scheme_end; i++) {
        if (!is_scheme_char(url[i]))
            return std::string("invalid scheme character");
    }
    u.scheme = to_lower(url.substr(0, scheme_end));
    pos = scheme_end + 1;

    // Parse authority (//)
    bool has_authority = false;
    if (pos + 1 < len && url[pos] == '/' && url[pos + 1] == '/') {
        has_authority = true;
        pos += 2;

        // Find end of authority
        std::size_t auth_end = len;
        for (std::size_t i = pos; i < len; i++) {
            if (url[i] == '/' || url[i] == '?' || url[i] == '#') {
                auth_end = i;
                break;
            }
        }

        std::string authority = url.substr(pos, auth_end - pos);
        pos = auth_end;

        // Parse userinfo@host:port
        std::size_t at_pos = authority.find('@');
        if (at_pos != std::string::npos) {
            std::string userinfo = authority.substr(0, at_pos);
            std::size_t colon = userinfo.find(':');
            u.username = url_decode(userinfo.substr(0, colon));
            if (colon != std::string::npos)
                u.password = url_decode(userinfo.substr(colon + 1));
            authority = authority.substr(at_pos + 1);
        }

        // Parse host:port (handle IPv6)
        std::size_t port_pos = std::string::npos;
        if (!authority.empty() && authority[0] == '[') {
            std::size_t closing = authority.find(']');
            if (closing == std::string::npos)
                return std::string("unclosed IPv6 address");
            u.host = authority.substr(0, closing + 1);
            if (closing + 1 < authority.size() && authority[closing + 1] == ':')
                port_pos = closing + 1;
        } else {
            port_pos = authority.rfind(':');
            if (port_pos != std::string::npos) {
                // Make sure it's not just a lone colon at position 0 (empty host)
                if (port_pos == 0) return std::string("empty host");
                u.host = authority.substr(0, port_pos);
            } else {
                u.host = authority;
            }
        }

        if (port_pos != std::string::npos) {
            std::string port_str = authority.substr(port_pos + 1);
            if (port_str.empty()) return std::string("empty port");
            char* end = nullptr;
            long p = std::strtol(port_str.c_str(), &end, 10);
            if (*end != '\0' || p <= 0 || p > 65535)
                return std::string("invalid port");
            u.port = static_cast<u16>(p);
        }

        u.host = to_lower(u.host);
    }

    // Parse path
    std::size_t query_start = url.find('?', pos);
    std::size_t fragment_start = url.find('#', pos);
    if (fragment_start != std::string::npos && query_start != std::string::npos && query_start > fragment_start)
        query_start = std::string::npos;

    std::size_t path_end = len;
    if (query_start != std::string::npos) path_end = query_start;
    if (fragment_start != std::string::npos && fragment_start < path_end) path_end = fragment_start;

    if (pos < path_end) {
        u.path = url.substr(pos, path_end - pos);
    } else if (has_authority) {
        u.path = "/";
    }

    // Parse query
    if (query_start != std::string::npos) {
        std::size_t qe = len;
        if (fragment_start != std::string::npos) qe = fragment_start;
        u.query = url.substr(query_start + 1, qe - query_start - 1);
    }

    // Parse fragment
    if (fragment_start != std::string::npos) {
        u.fragment = url.substr(fragment_start + 1);
    }

    if (u.host.empty() && u.path.empty())
        return std::string("empty URL");
    if (!u.host.empty() && u.port == 0) {
        u16 dp = u.default_port();
        if (dp != 0) u.port = dp;
    }

    return u;
}

static std::string normalize_path(const std::string& path) {
    std::vector<std::string> segs;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (i > start) {
                std::string s = path.substr(start, i - start);
                if (s == "..") { if (!segs.empty()) segs.pop_back(); }
                else if (s != ".") { segs.push_back(std::move(s)); }
            }
            start = i + 1;
        }
    }
    std::string r;
    for (auto& s : segs) r += "/" + s;
    return r.empty() ? "/" : r;
}

Result<URL> URL::resolve(const std::string& relative) const {
    // Try parsing as absolute first
    auto parsed = URL::parse(relative);
    if (parsed.is_ok()) return parsed;

    // Relative reference — RFC 3986 §5.2
    URL target = *this;

    std::size_t pos = 0;
    std::size_t len = relative.size();

    if (pos < len && relative[pos] == '?') {
        target.query = relative.substr(pos + 1);
        target.fragment.clear();
        return target;
    }

    if (pos < len && relative[pos] == '#') {
        target.fragment = relative.substr(pos + 1);
        return target;
    }

    // Scheme-relative: "//host/path"
    if (pos + 1 < len && relative[pos] == '/' && relative[pos + 1] == '/') {
        auto abs = URL::parse(target.scheme + ":" + relative);
        if (abs.is_ok()) {
            auto u = abs.unwrap();
            u.path = normalize_path(u.path);
            return u;
        }
        return std::string("resolve failed");
    }

    // Absolute path: "/path"
    if (pos < len && relative[pos] == '/') {
        auto abs = URL::parse(target.scheme + "://" + target.host +
                              (target.port != target.default_port() ? ":" + std::to_string(target.port) : "") +
                              relative);
        if (abs.is_ok()) {
            auto u = abs.unwrap();
            u.path = normalize_path(u.path);
            return u;
        }
        return std::string("resolve failed");
    }

    // Relative path — split into segments and resolve
    std::string base_path = target.path;
    if (base_path.empty()) base_path = "/";
    std::size_t last_slash = base_path.rfind('/');
    if (last_slash != std::string::npos) base_path = base_path.substr(0, last_slash + 1);
    else base_path = "/";

    std::string merged = base_path + relative;

    // Split path into segments and resolve "." and ".."
    std::vector<std::string> segments;
    std::size_t seg_start = 0;
    for (std::size_t i = 0; i <= merged.size(); i++) {
        if (i == merged.size() || merged[i] == '/') {
            if (i > seg_start) {
                std::string seg = merged.substr(seg_start, i - seg_start);
                if (seg == "..") {
                    if (!segments.empty()) segments.pop_back();
                } else if (seg != ".") {
                    segments.push_back(std::move(seg));
                }
            }
            seg_start = i + 1;
        }
    }

    // Reconstruct path
    std::string p;
    for (auto& seg : segments) {
        p += "/" + seg;
    }
    if (p.empty()) p = "/";
    target.path = p;
    target.query.clear();
    target.fragment.clear();
    return target;
}

} // namespace browser::net
