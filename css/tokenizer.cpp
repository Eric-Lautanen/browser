#include "tokenizer.hpp"
#include <cctype>
#include <cstdlib>

namespace browser::css {

CssTokenizer::CssTokenizer(const std::string& input) : input_(input) {}

bool CssTokenizer::has_next() const {
    return pos_ < input_.size();
}

char CssTokenizer::peek(u32 offset) const {
    u32 idx = pos_ + offset;
    if (idx >= input_.size()) return '\0';
    return input_[idx];
}

char CssTokenizer::advance() {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_++];
}

bool CssTokenizer::is_eof() const {
    return pos_ >= input_.size();
}

bool CssTokenizer::is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool CssTokenizer::is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool CssTokenizer::is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool CssTokenizer::is_ident_start(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return true;
    if (static_cast<unsigned char>(c) >= 0x80) return true;
    return false;
}

bool CssTokenizer::is_name_char(char c) {
    return is_ident_start(c) || is_digit(c) || c == '-';
}

bool CssTokenizer::starts_ident() const {
    if (is_eof()) return false;
    char c = peek();
    if (is_ident_start(c)) return true;
    if (c == '\\') return true;
    if (c == '-') {
        if (pos_ + 1 >= input_.size()) return false;
        char n = input_[pos_ + 1];
        if (is_ident_start(n) || n == '-' || n == '\\') return true;
    }
    return false;
}

std::string CssTokenizer::consume_escape() {
    advance();
    if (is_eof()) return "\xEF\xBF\xBD";
    if (is_hex_digit(peek())) {
        u32 code = 0;
        int count = 0;
        while (count < 6 && pos_ < input_.size() && is_hex_digit(input_[pos_])) {
            char c = input_[pos_];
            if (c >= '0' && c <= '9') code = code * 16 + static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') code = code * 16 + static_cast<u32>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code = code * 16 + static_cast<u32>(c - 'A' + 10);
            pos_++;
            count++;
        }
        if (pos_ < input_.size() && is_whitespace(input_[pos_])) {
            pos_++;
        }
        if (code == 0 || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
            return "\xEF\xBF\xBD";
        }
        if (code <= 0x7F) return std::string(1, static_cast<char>(code));
        std::string utf8;
        if (code <= 0x7FF) {
            utf8 += static_cast<char>(0xC0 | (code >> 6));
            utf8 += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code <= 0xFFFF) {
            utf8 += static_cast<char>(0xE0 | (code >> 12));
            utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            utf8 += static_cast<char>(0xF0 | (code >> 18));
            utf8 += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (code & 0x3F));
        }
        return utf8;
    }
    return std::string(1, advance());
}

std::string CssTokenizer::consume_ident() {
    std::string result;
    while (pos_ < input_.size()) {
        char c = input_[pos_];
        if (c == '\\' && pos_ + 1 < input_.size()) {
            result += consume_escape();
        } else if (is_name_char(c)) {
            result += advance();
        } else {
            break;
        }
    }
    return result;
}

CssToken CssTokenizer::consume_numeric() {
    std::string num_str;

    if (peek() == '-' || peek() == '+') {
        if (pos_ + 1 < input_.size() && is_digit(input_[pos_ + 1])) {
            num_str += advance();
        } else if (pos_ + 1 < input_.size() && input_[pos_ + 1] == '.') {
            num_str += advance();
        } else {
            return {CssTokenType::DELIM, std::string(1, advance()), 0};
        }
    }

    while (pos_ < input_.size() && is_digit(input_[pos_])) {
        num_str += advance();
    }

    if (pos_ + 1 < input_.size() && peek() == '.' && is_digit(input_[pos_ + 1])) {
        num_str += advance();
        while (pos_ < input_.size() && is_digit(input_[pos_])) {
            num_str += advance();
        }
    }

    if (!num_str.empty() && pos_ < input_.size() && (peek() == 'e' || peek() == 'E')) {
        if (pos_ + 1 < input_.size()) {
            char n = input_[pos_ + 1];
            if (is_digit(n) || n == '+' || n == '-') {
                num_str += advance();
                if ((peek() == '+' || peek() == '-') && pos_ < input_.size()) {
                    num_str += advance();
                }
                while (pos_ < input_.size() && is_digit(input_[pos_])) {
                    num_str += advance();
                }
            }
        }
    }

    f32 value = static_cast<f32>(std::atof(num_str.c_str()));

    if (peek() == '%') {
        advance();
        return {CssTokenType::PERCENTAGE, "%", value};
    }

    if (starts_ident()) {
        std::string unit = consume_ident();
        return {CssTokenType::DIMENSION, unit, value};
    }

    return {CssTokenType::NUMBER, num_str, value};
}

CssToken CssTokenizer::consume_string(char quote) {
    advance(); // consume opening quote
    std::string result;
    while (pos_ < input_.size()) {
        char c = input_[pos_];
        if (c == quote) {
            advance();
            return {CssTokenType::STRING, result, 0};
        }
        if (c == '\n' || c == '\r' || c == '\f') {
            return {CssTokenType::STRING, result, 0};
        }
        if (c == '\\') {
            if (pos_ + 1 < input_.size()) {
                char n = input_[pos_ + 1];
                if (n == '\n' || n == '\r' || n == '\f') {
                    advance();
                    if (n == '\r' && pos_ < input_.size() && input_[pos_] == '\n') advance();
                } else {
                    result += consume_escape();
                }
            } else {
                advance();
            }
        } else {
            result += advance();
        }
    }
    return {CssTokenType::STRING, result, 0};
}

CssToken CssTokenizer::consume_url() {
    std::string result;
    advance(); // (

    while (pos_ < input_.size() && is_whitespace(input_[pos_])) advance();

    if (peek() == '"' || peek() == '\'') {
        char q = advance();
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (c == q) { advance(); break; }
            if (c == '\\') {
                result += consume_escape();
            } else {
                result += advance();
            }
        }
        while (pos_ < input_.size() && is_whitespace(input_[pos_])) advance();
        if (peek() == ')') advance();
        return {CssTokenType::URL, result, 0};
    }

    while (pos_ < input_.size()) {
        char c = input_[pos_];
        if (c == ')') { advance(); break; }
        if (is_whitespace(c)) {
            while (pos_ < input_.size() && is_whitespace(input_[pos_])) advance();
            if (peek() == ')') { advance(); break; }
            break;
        }
        if (c == '"' || c == '\'') break;
        if (c == '\\') {
            result += consume_escape();
        } else {
            result += advance();
        }
    }
    return {CssTokenType::URL, result, 0};
}

CssToken CssTokenizer::consume_ident_like() {
    std::string ident = consume_ident();
    if (pos_ < input_.size() && input_[pos_] == '(') {
        std::string lower;
        for (char c : ident) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "url") {
            return consume_url();
        }
        advance();
        return {CssTokenType::FUNCTION, ident, 0};
    }
    return {CssTokenType::IDENT, ident, 0};
}

CssToken CssTokenizer::consume_hash() {
    advance(); // consume #
    std::string name = consume_ident();
    return {CssTokenType::HASH, name, 0};
}

CssToken CssTokenizer::consume_at_keyword() {
    advance(); // consume @
    std::string name = consume_ident();
    return {CssTokenType::AT_KEYWORD, name, 0};
}

void CssTokenizer::consume_comment() {
    advance(); // /
    advance(); // *
    while (pos_ + 1 < input_.size()) {
        if (input_[pos_] == '*' && input_[pos_ + 1] == '/') {
            pos_ += 2;
            return;
        }
        pos_++;
    }
    pos_ = static_cast<u32>(input_.size());
}

CssToken CssTokenizer::next() {
    while (true) {
        if (is_eof()) return {CssTokenType::EOF_TOKEN, "", 0};

        // Skip comments
        if (peek() == '/' && peek(1) == '*') {
            consume_comment();
            continue;
        }

        // Whitespace
        if (is_whitespace(peek())) {
            while (pos_ < input_.size() && is_whitespace(input_[pos_])) pos_++;
            return {CssTokenType::WHITESPACE, " ", 0};
        }

        // Strings
        if (peek() == '"' || peek() == '\'') return consume_string(peek());

        // Hash
        if (peek() == '#') {
            if (pos_ + 1 < input_.size() && (is_ident_start(input_[pos_ + 1]) || input_[pos_ + 1] == '\\')) {
                return consume_hash();
            }
            advance();
            return {CssTokenType::DELIM, "#", 0};
        }

        // At-keyword
        if (peek() == '@') {
            if (pos_ + 1 < input_.size() && (is_ident_start(input_[pos_ + 1]) || input_[pos_ + 1] == '\\')) {
                return consume_at_keyword();
            }
            advance();
            return {CssTokenType::DELIM, "@", 0};
        }

        // Number
        if (is_digit(peek()) || (peek() == '.' && is_digit(peek(1)))) {
            return consume_numeric();
        }

        if (peek() == '+' && is_digit(peek(1))) return consume_numeric();
        if (peek() == '-' && is_digit(peek(1))) return consume_numeric();
        if (peek() == '+' && peek(1) == '.' && is_digit(peek(2))) return consume_numeric();
        if (peek() == '-' && peek(1) == '.' && is_digit(peek(2))) return consume_numeric();

        // CDO/CDC: <!-- and --> at start of stylesheet (CSS Syntax spec)
        if (peek() == '<' && input_.substr(pos_, 4) == "<!--") {
            pos_ += 4;
            return {CssTokenType::WHITESPACE, " ", 0};
        }
        if (peek() == '-' && input_.substr(pos_, 3) == "-->") {
            pos_ += 3;
            return {CssTokenType::WHITESPACE, " ", 0};
        }

        // Ident-like
        if (starts_ident()) return consume_ident_like();

        // Single character tokens
        char c = advance();
        switch (c) {
            case ':': return {CssTokenType::COLON, ":", 0};
            case ';': return {CssTokenType::SEMICOLON, ";", 0};
            case ',': return {CssTokenType::COMMA, ",", 0};
            case '{': return {CssTokenType::OPEN_BRACE, "{", 0};
            case '}': return {CssTokenType::CLOSE_BRACE, "}", 0};
            case '(': return {CssTokenType::OPEN_PAREN, "(", 0};
            case ')': return {CssTokenType::CLOSE_PAREN, ")", 0};
            case '[': return {CssTokenType::OPEN_BRACKET, "[", 0};
            case ']': return {CssTokenType::CLOSE_BRACKET, "]", 0};
        }

        return {CssTokenType::DELIM, std::string(1, c), 0};
    }
}

}
