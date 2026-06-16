#pragma once
#include "../tests/utility.hpp"
#include <string>
#include <vector>

namespace browser::net {

struct URL {
    std::string scheme;
    std::string username;
    std::string password;
    std::string host;
    u16 port = 0;
    std::string path;
    std::string query;
    std::string fragment;

    std::string to_string() const;
    u16 default_port() const;
    static Result<URL> parse(const std::string& url);
    Result<URL> resolve(const std::string& relative) const;
};

std::string url_encode(const std::string& s);
std::string url_decode(const std::string& s);

} // namespace browser::net
