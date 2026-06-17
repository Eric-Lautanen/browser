#pragma once
#include "../tests/utility.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

namespace browser::net {

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    bool secure = false;
    bool httpOnly = false;
    std::string sameSite;
    u64 expires_time = 0;
    u64 creation_time = 0;
    u64 last_access_time = 0;
};

class CookieJar {
public:
    CookieJar();
    ~CookieJar();

    void set_cookie(const std::string& request_domain, const std::string& request_path, const Cookie& c);
    std::vector<Cookie> get_cookies(const std::string& domain, const std::string& path, bool secure_only) const;
    std::vector<Cookie> get_cookies_for_js(const std::string& domain, const std::string& path, bool secure_only) const;
    bool is_expired(const Cookie& c) const;
    Result<void> load_from_file(const std::string& path);
    Result<void> save_to_file(const std::string& path);
    void clear_expired();
    void clear();

private:
    std::vector<Cookie> cookies_;

    static bool domain_match(const std::string& cookie_domain, const std::string& request_domain);
    static bool path_match(const std::string& cookie_path, const std::string& request_path);
    static std::string trim(const std::string& s);
    static std::string to_lower(const std::string& s);
};

} // namespace browser::net
