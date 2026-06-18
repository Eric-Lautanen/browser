#pragma once
#include "../tests/utility.hpp"
#include <string>

namespace browser::html {

struct DecodeResult {
    char32_t codepoint = 0;
    u32 bytes_consumed = 0;
};

DecodeResult decode_utf8(const u8* data, u32 len);

inline bool is_emoji(char32_t cp) {
    return (cp >= 0x2600 && cp <= 0x27BF) ||
           (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) ||
           cp == 0x200D ||
           (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}

inline std::string encode_utf8(char32_t cp) {
    std::string r;
    if (cp < 0x80) {
        r += static_cast<char>(cp);
    } else if (cp < 0x800) {
        r += static_cast<char>(0xC0 | (cp >> 6));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        r += static_cast<char>(0xE0 | (cp >> 12));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        r += static_cast<char>(0xF0 | (cp >> 18));
        r += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        r += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return r;
}

} // namespace browser::html
