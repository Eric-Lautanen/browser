#include "tokenizer.hpp"

namespace browser::html {

    static i32 ent_cmp(const char *a, const char *b) {
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return 0;
        if (*a == '\0')
            return -1;
        if (*b == '\0')
            return 1;
        return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
    }

    static std::string utf8_encode(char32_t cp) {
        std::string s;
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xEF);
            s += static_cast<char>(0xBF);
            s += static_cast<char>(0xBD);
        }
        return s;
    }

    static const NamedEntity *lookup_entity(const char *name) {
        i32 lo = 0;
        i32 hi = static_cast<i32>(HTML_ENTITIES_COUNT) - 1;
        while (lo <= hi) {
            i32 mid = lo + (hi - lo) / 2;
            i32 cmp = ent_cmp(name, HTML_ENTITIES[mid].name);
            if (cmp == 0)
                return &HTML_ENTITIES[mid];
            if (cmp < 0)
                hi = mid - 1;
            else
                lo = mid + 1;
        }
        return nullptr;
    }

    void Tokenizer::consume_char_ref() {
        u32 start = pos_;
        bool in_attr = (return_state_ == State::ATTRIBUTE_VALUE_DQ || return_state_ == State::ATTRIBUTE_VALUE_SQ ||
                        return_state_ == State::ATTRIBUTE_VALUE_UQ);

        auto emit_or_buffer = [&](char32_t cp) {
            if (in_attr) {
                temporary_buffer_ += utf8_encode(cp);
            } else {
                emit_char(cp);
            }
        };

        if (is_eof()) {
            emit_or_buffer('&');
            return;
        }

        if (current() == '#') {
            advance();
            char32_t cp = 0;
            bool hex = false;
            bool overflow = false;
            if (current() == 'x' || current() == 'X') {
                hex = true;
                advance();
            }
            while (!is_eof()) {
                char c = current();
                u32 digit = 0xFFFFFFFF;
                if (c >= '0' && c <= '9')
                    digit = static_cast<u32>(c - '0');
                else if (hex && c >= 'a' && c <= 'f')
                    digit = static_cast<u32>(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F')
                    digit = static_cast<u32>(c - 'A' + 10);
                else
                    break;
                if (!overflow) {
                    u32 base = hex ? 16 : 10;
                    if (cp > (0x10FFFF / base)) {
                        overflow = true;
                    } else {
                        cp = static_cast<char32_t>(cp * base + digit);
                        if (cp > 0x10FFFF)
                            overflow = true;
                    }
                }
                advance();
            }
            if (!consume_if(';')) {
                pos_ = start;
                emit_or_buffer('&');
                return;
            }
            if (overflow || cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                cp = kReplacementChar;
            }
            emit_or_buffer(cp);
            return;
        }

        u32 saved = pos_;
        std::string name_buf;
        while (!is_eof()) {
            char c = current();
            if (is_ascii_alnum(c)) {
                name_buf += c;
                advance();
            } else
                break;
        }

        while (!name_buf.empty()) {
            std::string with_semi = name_buf + ";";
            const NamedEntity *ent = lookup_entity(with_semi.c_str());
            if (ent && consume_if(';')) {
                emit_or_buffer(ent->codepoint);
                return;
            }

            ent = lookup_entity(name_buf.c_str());
            if (ent) {
                char next = current();
                if (is_ascii_alnum(next) || next == '=') {
                    name_buf.pop_back();
                    pos_ = saved + static_cast<u32>(name_buf.size());
                    continue;
                }
                emit_or_buffer(ent->codepoint);
                return;
            }

            name_buf.pop_back();
            pos_ = saved + static_cast<u32>(name_buf.size());
        }

        pos_ = saved;
        emit_or_buffer('&');
    }

    void Tokenizer::process_data_state(char c) {
        if (c == '&') {
            return_state_ = State::DATA;
            state_ = State::CHARACTER_REFERENCE;
        } else if (c == '<') {
            state_ = State::TAG_OPEN;
        } else if (c == '\0') {
            if (is_eof()) {
                emit_eof();
            } else {
                emit_char(kReplacementChar);
            }
        } else {
            emit_char(static_cast<unsigned char>(c));
        }
    }

    void Tokenizer::process_char_ref_state() {
        if (pos_ > 0)
            pos_--;
        consume_char_ref();
        state_ = return_state_;
    }

}  // namespace browser::html
