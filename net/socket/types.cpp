#include "types.hpp"

#include "socket_win32.hpp"

#include <sstream>

namespace browser::net {

    std::string IPv4Address::to_string() const {
        std::ostringstream oss;
        oss << static_cast<int>(octets[0]) << '.' << static_cast<int>(octets[1]) << '.' << static_cast<int>(octets[2])
            << '.' << static_cast<int>(octets[3]);
        return oss.str();
    }

    Result<IPv4Address> IPv4Address::from_string(const std::string &s) {
        IPv4Address addr;
        u32 parts[4] = {};
        int count = 0;
        std::string current;
        for (char c : s) {
            if (c == '.') {
                if (count >= 4)
                    return std::string("invalid IPv4 address");
                parts[count++] = static_cast<u32>(std::stoul(current));
                current.clear();
            } else if (c >= '0' && c <= '9') {
                current += c;
            } else {
                return std::string("invalid IPv4 address");
            }
        }
        if (count != 3 || current.empty())
            return std::string("invalid IPv4 address");
        parts[count] = static_cast<u32>(std::stoul(current));
        for (int i = 0; i < 4; i++) {
            if (parts[i] > 255)
                return std::string("invalid IPv4 address");
            addr.octets[i] = static_cast<u8>(parts[i]);
        }
        return addr;
    }

    bool IPv4Address::operator==(const IPv4Address &other) const {
        return octets[0] == other.octets[0] && octets[1] == other.octets[1] && octets[2] == other.octets[2] &&
               octets[3] == other.octets[3];
    }

    IPv6Address::IPv6Address() {
        for (auto &g : groups) g = 0;
    }

    std::string IPv6Address::to_string() const {
        std::ostringstream oss;
        for (int i = 0; i < 8; i++) {
            if (i > 0)
                oss << ':';
            oss << std::hex << groups[i];
        }
        return oss.str();
    }

    Result<IPv6Address> IPv6Address::from_string(const std::string &s) {
        IPv6Address addr;
        u16 parsed[8];
        int num_parsed = 0;
        int double_colon_idx = -1;
        std::string current;

        auto flush = [&]() -> Result<void> {
            if (current.empty())
                return {};
            if (num_parsed >= 8)
                return std::string("too many IPv6 groups");
            parsed[num_parsed++] = static_cast<u16>(std::stoul(current, nullptr, 16));
            current.clear();
            return {};
        };

        for (std::size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == ':') {
                if (i + 1 < s.size() && s[i + 1] == ':') {
                    auto r = flush();
                    if (r.is_err())
                        return r.unwrap_err();
                    double_colon_idx = num_parsed;
                    i++;
                } else {
                    auto r = flush();
                    if (r.is_err())
                        return r.unwrap_err();
                }
            } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                current += c;
            } else {
                return std::string("invalid IPv6 character");
            }
        }
        {
            auto r = flush();
            if (r.is_err())
                return r.unwrap_err();
        }

        if (double_colon_idx >= 0) {
            int zeros = 8 - num_parsed;
            for (int i = num_parsed - 1; i >= double_colon_idx; i--) {
                parsed[i + zeros] = parsed[i];
            }
            for (int i = 0; i < zeros; i++) {
                parsed[double_colon_idx + i] = 0;
            }
            num_parsed = 8;
        }

        if (num_parsed != 8) {
            return std::string("invalid IPv6 address: wrong group count");
        }

        for (int i = 0; i < 8; i++) {
            addr.groups[i] = parsed[i];
        }
        return addr;
    }

    bool IPv6Address::operator==(const IPv6Address &other) const {
        for (int i = 0; i < 8; i++) {
            if (groups[i] != other.groups[i])
                return false;
        }
        return true;
    }

    Result<std::unique_ptr<Socket>> Socket::create_tcp() {
        return std::make_unique<Win32TCPSocket>();
    }

    Result<std::unique_ptr<UDPSocket>> UDPSocket::create() {
        return std::make_unique<Win32UDPSocket>();
    }

}  // namespace browser::net
