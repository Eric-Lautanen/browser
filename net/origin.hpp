#pragma once
#include "../tests/utility.hpp"
#include "url.hpp"

#include <functional>
#include <string>

namespace browser::net {

    struct Origin {
        std::string scheme;
        std::string host;
        u16 port = 0;

        static Origin from_url(const URL &url);
        static Origin from_url_str(const std::string &url_str);

        bool is_same_origin(const Origin &other) const;
        std::string to_string() const;
        std::string serialized() const;

        bool operator==(const Origin &other) const;
    };

}  // namespace browser::net

namespace std {
    template <>
    struct hash<browser::net::Origin> {
        size_t operator()(const browser::net::Origin &o) const {
            size_t h1 = hash<string>()(o.scheme);
            size_t h2 = hash<string>()(o.host);
            size_t h3 = hash<uint16_t>()(o.port);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}  // namespace std
