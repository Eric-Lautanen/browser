#include "cookie_jar.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <cctype>

namespace browser::net {

static u64 now_epoch_secs() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

CookieJar::CookieJar() = default;
CookieJar::~CookieJar() = default;

std::string CookieJar::trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    return s.substr(start, end - start);
}

std::string CookieJar::to_lower(const std::string& s) {
    std::string r;
    r.resize(s.size());
    for (size_t i = 0; i < s.size(); i++)
        r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return r;
}

bool CookieJar::domain_match(const std::string& cookie_domain, const std::string& request_domain) {
    std::string cd = to_lower(cookie_domain);
    std::string rd = to_lower(request_domain);
    if (cd.empty()) return false;
    if (cd == rd) return true;
    if (cd.front() == '.') {
        if (rd.size() >= cd.size() && rd.substr(rd.size() - cd.size()) == cd)
            return true;
    }
    return false;
}

bool CookieJar::path_match(const std::string& cookie_path, const std::string& request_path) {
    if (cookie_path.empty()) return true;
    if (request_path.size() < cookie_path.size()) return false;
    if (request_path.substr(0, cookie_path.size()) != cookie_path) return false;
    if (request_path.size() > cookie_path.size() && cookie_path.back() != '/' && request_path[cookie_path.size()] != '/')
        return false;
    return true;
}

bool CookieJar::is_expired(const Cookie& c) const {
    if (c.expires_time == 0) return false;
    return now_epoch_secs() >= c.expires_time;
}

void CookieJar::set_cookie(const std::string& request_domain, const std::string& request_path, const Cookie& c) {
    Cookie cookie = c;
    cookie.creation_time = now_epoch_secs();
    cookie.last_access_time = cookie.creation_time;

    if (!cookie.path.empty() && cookie.path.front() != '/')
        cookie.path = "/";

    if (cookie.path.empty())
        cookie.path = request_path;

    auto slash_pos = cookie.path.rfind('/');
    if (slash_pos != std::string::npos && slash_pos > 0)
        cookie.path = cookie.path.substr(0, slash_pos);
    if (cookie.path.empty())
        cookie.path = "/";

    if (cookie.domain.empty()) {
        cookie.domain = request_domain;
    }

    // Remove duplicate (matching name + domain + path)
    for (auto it = cookies_.begin(); it != cookies_.end();) {
        if (it->name == cookie.name &&
            to_lower(it->domain) == to_lower(cookie.domain) &&
            it->path == cookie.path) {
            it = cookies_.erase(it);
        } else {
            ++it;
        }
    }

    cookies_.push_back(std::move(cookie));
}

std::vector<Cookie> CookieJar::get_cookies(const std::string& domain, const std::string& path, bool secure_only) const {
    std::vector<Cookie> result;

    for (auto& c : cookies_) {
        if (is_expired(c)) continue;
        if (!domain_match(c.domain, domain)) continue;
        if (!path_match(c.path, path)) continue;
        if (c.secure && !secure_only) continue;

        result.push_back(c);
    }

    std::sort(result.begin(), result.end(), [](const Cookie& a, const Cookie& b) {
        if (a.path.size() != b.path.size())
            return a.path.size() > b.path.size();
        return a.creation_time < b.creation_time;
    });

    return result;
}

std::vector<Cookie> CookieJar::get_cookies_for_js(const std::string& domain, const std::string& path, bool secure_only) const {
    std::vector<Cookie> result;

    for (auto& c : cookies_) {
        if (is_expired(c)) continue;
        if (c.httpOnly) continue;
        if (!domain_match(c.domain, domain)) continue;
        if (!path_match(c.path, path)) continue;
        if (c.secure && !secure_only) continue;

        result.push_back(c);
    }

    std::sort(result.begin(), result.end(), [](const Cookie& a, const Cookie& b) {
        if (a.path.size() != b.path.size())
            return a.path.size() > b.path.size();
        return a.creation_time < b.creation_time;
    });

    return result;
}

void CookieJar::clear_expired() {
    u64 now = now_epoch_secs();
    for (auto it = cookies_.begin(); it != cookies_.end();) {
        if (it->expires_time != 0 && now >= it->expires_time) {
            it = cookies_.erase(it);
        } else {
            ++it;
        }
    }
}

void CookieJar::clear() {
    cookies_.clear();
}

Result<void> CookieJar::load_from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};

    cookies_.clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string domain, domain_flag, path_str, secure_flag, expires_str, name, value;

        if (!std::getline(ss, domain, '\t')) continue;
        if (!std::getline(ss, domain_flag, '\t')) continue;
        if (!std::getline(ss, path_str, '\t')) continue;
        if (!std::getline(ss, secure_flag, '\t')) continue;
        if (!std::getline(ss, expires_str, '\t')) continue;
        if (!std::getline(ss, name, '\t')) continue;
        if (!std::getline(ss, value, '\t')) continue;

        Cookie c;
        c.name = name;
        c.value = value;
        c.domain = domain;
        c.path = path_str;
        c.secure = (secure_flag == "TRUE");
        c.expires_time = static_cast<u64>(std::stoull(expires_str));
        c.creation_time = now_epoch_secs();
        c.last_access_time = c.creation_time;

        cookies_.push_back(std::move(c));
    }

    return {};
}

Result<void> CookieJar::save_to_file(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return std::string("cannot open " + path + " for writing");

    f << "# Netscape HTTP Cookie File\n";
    f << "# https://curl.se/rfc/cookie_spec.html\n";
    f << "# This is a generated file! Do not edit.\n";

    clear_expired();

    for (auto& c : cookies_) {
        if (c.domain.empty()) continue;
        std::string domain_flag = c.domain.front() == '.' ? "TRUE" : "FALSE";
        std::string secure_flag = c.secure ? "TRUE" : "FALSE";
        std::string expires_str = std::to_string(c.expires_time);

        f << c.domain << "\t" << domain_flag << "\t"
          << c.path << "\t" << secure_flag << "\t"
          << expires_str << "\t"
          << c.name << "\t" << c.value << "\n";
    }

    if (!f.good()) return std::string("write error");
    return {};
}

} // namespace browser::net
