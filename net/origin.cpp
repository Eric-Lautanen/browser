#include "origin.hpp"

namespace browser::net {

    Origin Origin::from_url(const URL &url) {
        Origin o;
        o.scheme = url.scheme;
        o.host = url.host;
        if (url.port != 0) {
            o.port = url.port;
        } else {
            o.port = url.default_port();
        }
        return o;
    }

    Origin Origin::from_url_str(const std::string &url_str) {
        auto parsed = URL::parse(url_str);
        if (parsed.is_ok()) {
            return from_url(parsed.unwrap());
        }
        return {};
    }

    bool Origin::is_same_origin(const Origin &other) const {
        if (scheme != other.scheme)
            return false;
        if (host != other.host)
            return false;
        if (port != other.port)
            return false;
        return true;
    }

    std::string Origin::to_string() const {
        std::string result = scheme + "://" + host;
        if (port != 0) {
            result += ":" + std::to_string(port);
        }
        return result;
    }

    std::string Origin::serialized() const {
        return to_string();
    }

    bool Origin::operator==(const Origin &other) const {
        return is_same_origin(other);
    }

}  // namespace browser::net
