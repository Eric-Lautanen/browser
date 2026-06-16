#include "utf8.hpp"

namespace browser::html {

DecodeResult decode_utf8(const u8* data, u32 len) {
    DecodeResult r;
    if (len == 0) { r.codepoint = 0xFFFD; r.bytes_consumed = 1; return r; }

    u8 first = data[0];
    if (first < 0x80) {
        r.codepoint = first;
        r.bytes_consumed = 1;
        return r;
    }

    // Determine sequence length from leading byte
    u32 seq_len = 0;
    char32_t min_cp = 0;
    char32_t cp = 0;
    if ((first & 0xE0) == 0xC0) { seq_len = 2; min_cp = 0x80; cp = first & 0x1F; }
    else if ((first & 0xF0) == 0xE0) { seq_len = 3; min_cp = 0x800; cp = first & 0x0F; }
    else if ((first & 0xF8) == 0xF0) { seq_len = 4; min_cp = 0x10000; cp = first & 0x07; }
    else { r.codepoint = 0xFFFD; r.bytes_consumed = 1; return r; }

    if (len < seq_len) {
        r.codepoint = 0xFFFD;
        r.bytes_consumed = len;
        return r;
    }

    for (u32 i = 1; i < seq_len; i++) {
        u8 b = data[i];
        if ((b & 0xC0) != 0x80) {
            r.codepoint = 0xFFFD;
            r.bytes_consumed = i;
            return r;
        }
        cp = (cp << 6) | static_cast<char32_t>(b & 0x3F);
    }

    // Overlong sequence: codepoint < min_cp for the sequence length
    if (cp < min_cp) { r.codepoint = 0xFFFD; r.bytes_consumed = seq_len; return r; }

    // Surrogate range: U+D800–U+DFFF
    if (cp >= 0xD800 && cp <= 0xDFFF) { r.codepoint = 0xFFFD; r.bytes_consumed = seq_len; return r; }

    // Beyond Unicode max
    if (cp > 0x10FFFF) { r.codepoint = 0xFFFD; r.bytes_consumed = seq_len; return r; }

    r.codepoint = cp;
    r.bytes_consumed = seq_len;
    return r;
}

} // namespace browser::html
