#include "json_writer.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>

namespace json {

std::string esc(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n"; break;
                case '\r': o += "\\r"; break;
                case '\t': o += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof buf, "\\u%04x", c);
                        o += buf;
                    } else {
                        o += static_cast<char>(c);
                    }
            }
        } else if ((c & 0xF8) == 0xF0) {
            auto dr = browser::html::decode_utf8(reinterpret_cast<const uint8_t *>(s.data()) + i,
                                                 static_cast<uint32_t>(s.size() - i));
            i += dr.bytes_consumed - 1;
            for (uint32_t j = 0; j < dr.bytes_consumed; j++) {
                o += s[i - (dr.bytes_consumed - 1) + j];
            }
        } else {
            o += static_cast<char>(c);
        }
    }
    return o;
}

std::string q(const std::string &s) { return "\"" + esc(s) + "\""; }

std::string num(float n) {
    if (std::isnan(n) || std::isinf(n)) return "null";
    std::ostringstream os;
    os << std::fixed << std::setprecision(4) << n;
    std::string s = os.str();
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        auto last = s.find_last_not_of('0');
        if (last > dot) s = s.substr(0, last + 1);
        else if (last == dot) s = s.substr(0, dot);
    }
    return s;
}

std::string bool_str(bool b) { return b ? "true" : "false"; }

void Obj::nl(bool inner) {
    if (indent >= 0) {
        int level = inner ? indent + 1 : indent;
        data += "\n" + std::string(level * 2, ' ');
    }
}
void Obj::kv(const std::string &k, const std::string &v) {
    if (!first) data += ",";
    first = false; nl();
    data += q(k) + (indent >= 0 ? ": " : ":") + q(v);
}
void Obj::kv_raw(const std::string &k, const std::string &v) {
    if (!first) data += ",";
    first = false; nl();
    data += q(k) + (indent >= 0 ? ": " : ":") + v;
}
void Obj::kv_num(const std::string &k, float v) {
    if (!first) data += ",";
    first = false; nl();
    data += q(k) + (indent >= 0 ? ": " : ":") + num(v);
}
void Obj::kv_bool(const std::string &k, bool v) {
    if (!first) data += ",";
    first = false; nl();
    data += q(k) + (indent >= 0 ? ": " : ":") + bool_str(v);
}
std::string Obj::done() {
    if (indent >= 0 && !first) nl(false);
    data += "}";
    return data;
}
void Arr::nl(bool inner) {
    if (indent >= 0) {
        int level = inner ? indent + 1 : indent;
        data += "\n" + std::string(level * 2, ' ');
    }
}
void Arr::push(const std::string &v) {
    if (!first) data += ",";
    first = false; nl();
    data += v;
}
std::string Arr::done() {
    if (indent >= 0 && !first) nl(false);
    data += "]";
    return data;
}

} // namespace json
