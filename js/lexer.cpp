#include "lexer.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unordered_map>

namespace browser::js {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool Lexer::is_digit(char c) { return c >= '0' && c <= '9'; }
bool Lexer::is_hex_digit(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool Lexer::is_oct_digit(char c) { return c >= '0' && c <= '7'; }
bool Lexer::is_bin_digit(char c) { return c == '0' || c == '1'; }

bool Lexer::is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

bool Lexer::is_identifier_char(char c) {
    return is_identifier_start(c) || is_digit(c);
}

bool Lexer::is_regexp_keyword(const std::string& text) {
    return text == "return" || text == "throw" || text == "typeof" ||
           text == "instanceof" || text == "void" || text == "delete" ||
           text == "new" || text == "in" || text == "of";
}

u8 Lexer::hex_val(char c) {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
    return 0;
}

void Lexer::append_utf8(std::string& s, char32_t cp) {
    if (cp <= 0x7F) {
        s += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += '\xEF'; s += '\xBF'; s += '\xBD';
    }
}

void Lexer::parse_unicode_escape(const std::string& src, u32& pos, std::string& out) {
    char32_t code = 0;
    if (pos < src.size() && src[pos] == '{') {
        pos++;
        while (pos < src.size() && src[pos] != '}') {
            code = code * 16 + hex_val(src[pos]);
            pos++;
        }
        if (pos < src.size()) pos++;
    } else {
        for (int i = 0; i < 4 && pos < src.size(); i++) {
            code = code * 16 + hex_val(src[pos]);
            pos++;
        }
    }
    append_utf8(out, code);
}

// ---------------------------------------------------------------------------
// Keyword / literal map
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, TokenType>& keyword_map() {
    static const std::unordered_map<std::string, TokenType> map = {
        {"true", TokenType::BOOLEAN},
        {"false", TokenType::BOOLEAN},
        {"null", TokenType::NULL_LITERAL},
        {"undefined", TokenType::UNDEFINED},
    };
    return map;
}

// ---------------------------------------------------------------------------
// Construction & core helpers
// ---------------------------------------------------------------------------

Lexer::Lexer(const std::string& source) : source_(source) {}

char Lexer::peek_char(u32 offset) const {
    u32 idx = pos_ + offset;
    if (idx >= source_.size()) return '\0';
    return source_[idx];
}

char Lexer::advance() {
    if (pos_ >= source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else if (c == '\r') {
        line_++;
        column_ = 1;
        if (pos_ < source_.size() && source_[pos_] == '\n') pos_++;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::eof() const {
    return pos_ >= source_.size();
}

Token Lexer::make_token(TokenType type, const std::string& text) {
    Token t;
    t.type = type;
    t.text = text;
    t.line = line_;
    t.column = column_;
    return t;
}

Token Lexer::make_eof() {
    Token t;
    t.type = TokenType::EOF_TOKEN;
    t.line = line_;
    t.column = column_;
    return t;
}

// ---------------------------------------------------------------------------
// Whitespace / comment skipping
// ---------------------------------------------------------------------------

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        char c = peek_char();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f') {
            advance();
        } else if (c == '\n') {
            advance();
        } else if (c == '/') {
            if (peek_char(1) == '/') {
                advance(); advance();
                while (!eof() && peek_char() != '\n') advance();
            } else if (peek_char(1) == '*') {
                advance(); advance();
                while (!eof()) {
                    if (peek_char() == '*' && peek_char(1) == '/') {
                        advance(); advance();
                        break;
                    }
                    advance();
                }
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Token readers
// ---------------------------------------------------------------------------

Token Lexer::read_number() {
    u32 start_line = line_;
    u32 start_col = column_;
    u32 start_pos = pos_;
    int base = 10;

    if (peek_char() == '0') {
        char n = peek_char(1);
        if (n == 'x' || n == 'X') { base = 16; advance(); advance(); }
        else if (n == 'o' || n == 'O') { base = 8; advance(); advance(); }
        else if (n == 'b' || n == 'B') { base = 2; advance(); advance(); }
    }

    if (base == 16) {
        while (is_hex_digit(peek_char())) advance();
    } else if (base == 8) {
        while (is_oct_digit(peek_char())) advance();
    } else if (base == 2) {
        while (is_bin_digit(peek_char())) advance();
    } else {
        while (is_digit(peek_char())) advance();
        if (peek_char() == '.') {
            advance();
            while (is_digit(peek_char())) advance();
        }
        if (peek_char() == 'e' || peek_char() == 'E') {
            advance();
            if (peek_char() == '+' || peek_char() == '-') advance();
            while (is_digit(peek_char())) advance();
        }
    }

    bool is_bigint = false;
    if (peek_char() == 'n') {
        is_bigint = true;
        advance();
    }

    std::string text = source_.substr(start_pos, pos_ - start_pos);
    f64 value = 0;
    if (!is_bigint) {
        if (base == 10) {
            value = std::strtod(text.c_str(), nullptr);
        } else {
            // Strip the 0x/0o/0b prefix before parsing
            const char* parse_start = text.c_str();
            if (text.size() >= 2 && text[0] == '0') {
                char p = text[1];
                if (p == 'x' || p == 'X' || p == 'o' || p == 'O' || p == 'b' || p == 'B') {
                    parse_start = text.c_str() + 2;
                }
            }
            value = static_cast<f64>(std::strtoull(parse_start, nullptr, base));
        }
    }

    Token t;
    t.type = is_bigint ? TokenType::BIGINT : TokenType::NUMBER;
    t.text = text;
    t.numeric_value = value;
    t.line = start_line;
    t.column = start_col;
    return t;
}

Token Lexer::read_string(char quote) {
    u32 start_line = line_;
    u32 start_col = column_;
    std::string text;
    advance();

    while (!eof()) {
        char c = advance();
        if (c == quote) {
            Token t;
            t.type = TokenType::STRING;
            t.text = text;
            t.line = start_line;
            t.column = start_col;
            return t;
        }
        if (c == '\\') {
            if (eof()) break;
            char n = advance();
            switch (n) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case 'r': text += '\r'; break;
                case 'b': text += '\b'; break;
                case 'f': text += '\f'; break;
                case 'v': text += '\v'; break;
                case '0': text += '\0'; break;
                case '\\': text += '\\'; break;
                case '\'': text += '\''; break;
                case '"': text += '"'; break;
                case '`': text += '`'; break;
                case 'x': {
                    if (pos_ < source_.size() && is_hex_digit(source_[pos_])) {
                        u8 hi = hex_val(advance());
                        u8 lo = 0;
                        if (pos_ < source_.size() && is_hex_digit(source_[pos_])) {
                            lo = hex_val(advance());
                        }
                        text += static_cast<char>((hi << 4) | lo);
                    }
                    break;
                }
                case 'u':
                    parse_unicode_escape(source_, pos_, text);
                    break;
                default:
                    text += n;
                    break;
            }
        } else if (c == '\n' || c == '\r') {
            break;
        } else {
            text += c;
        }
    }

    Token t;
    t.type = TokenType::STRING;
    t.text = text;
    t.line = start_line;
    t.column = start_col;
    return t;
}

Token Lexer::read_template(TokenType head_type) {
    u32 start_line = line_;
    u32 start_col = column_;
    std::string text;

    while (!eof()) {
        char c = peek_char();
        if (c == '`') {
            advance();
            Token t;
            t.type = TokenType::TEMPLATE_TAIL;
            t.text = text;
            t.line = start_line;
            t.column = start_col;
            template_state_ = false;
            template_brace_depth_ = 0;
            return t;
        }
        if (c == '$' && peek_char(1) == '{') {
            advance(); advance();
            Token t;
            t.type = head_type;
            t.text = text;
            t.line = start_line;
            t.column = start_col;
            template_state_ = true;
            return t;
        }
        if (c == '\\') {
            text += advance();
            if (!eof()) text += advance();
        } else {
            text += advance();
        }
    }

    Token t;
    t.type = TokenType::TEMPLATE_TAIL;
    t.text = text;
    t.line = start_line;
    t.column = start_col;
    template_state_ = false;
    template_brace_depth_ = 0;
    return t;
}

Token Lexer::read_identifier_or_keyword() {
    u32 start_line = line_;
    u32 start_col = column_;
    std::string text;

    while (!eof()) {
        char c = peek_char();
        if (is_identifier_char(c)) {
            text += advance();
        } else if (c == '\\' && peek_char(1) == 'u') {
            advance();
            advance();
            parse_unicode_escape(source_, pos_, text);
        } else {
            break;
        }
    }

    TokenType type = TokenType::IDENTIFIER;
    auto& kmap = keyword_map();
    auto it = kmap.find(text);
    if (it != kmap.end()) {
        type = it->second;
    }

    Token t;
    t.type = type;
    t.text = text;
    t.line = start_line;
    t.column = start_col;
    return t;
}

Token Lexer::read_regexp() {
    u32 start_line = line_;
    u32 start_col = column_;
    std::string text;
    text += advance();
    bool in_class = false;

    while (!eof()) {
        char c = advance();
        text += c;
        if (c == '[' && !in_class) {
            in_class = true;
        } else if (c == ']') {
            in_class = false;
        } else if (c == '/' && !in_class) {
            break;
        } else if (c == '\\') {
            if (!eof()) text += advance();
        }
    }

    while (!eof()) {
        char c = peek_char();
        if (is_identifier_char(c)) {
            text += advance();
        } else {
            break;
        }
    }

    Token t;
    t.type = TokenType::REGEXP;
    t.text = text;
    t.line = start_line;
    t.column = start_col;
    return t;
}

Token Lexer::read_punctuator() {
    u32 start_line = line_;
    u32 start_col = column_;
    char c = advance();
    std::string txt(1, c);

    auto tk = [&](TokenType type, const std::string& text) -> Token {
        Token t; t.type = type; t.text = text; t.line = start_line; t.column = start_col; return t;
    };

    switch (c) {
        case '=':
            if (peek_char() == '=') {
                txt += advance();
                if (peek_char() == '=') { txt += advance(); return tk(TokenType::EQ_EQ_EQ, txt); }
                return tk(TokenType::EQ_EQ, txt);
            }
            if (peek_char() == '>') { txt += advance(); return tk(TokenType::ARROW, txt); }
            return tk(TokenType::EQUALS, txt);
        case '!':
            if (peek_char() == '=') {
                txt += advance();
                if (peek_char() == '=') { txt += advance(); return tk(TokenType::NOT_EQ_EQ, txt); }
                return tk(TokenType::NOT_EQ, txt);
            }
            return tk(TokenType::NOT, txt);
        case '<':
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::LT_EQ, txt); }
            return tk(TokenType::LT, txt);
        case '>':
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::GT_EQ, txt); }
            return tk(TokenType::GT, txt);
        case '+':
            if (peek_char() == '+') { txt += advance(); return tk(TokenType::PLUS_PLUS, txt); }
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::PLUS_EQ, txt); }
            return tk(TokenType::PLUS, txt);
        case '-':
            if (peek_char() == '-') { txt += advance(); return tk(TokenType::MINUS_MINUS, txt); }
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::MINUS_EQ, txt); }
            return tk(TokenType::MINUS, txt);
        case '|':
            if (peek_char() == '|') { txt += advance(); return tk(TokenType::OR_OR, txt); }
            return tk(TokenType::PIPE, txt);
        case '&':
            if (peek_char() == '&') { txt += advance(); return tk(TokenType::AND_AND, txt); }
            return tk(TokenType::AMPERSAND, txt);
        case '?':
            if (peek_char() == '?') { txt += advance(); return tk(TokenType::NULLISH_COALESCING, txt); }
            if (peek_char() == '.') { txt += advance(); return tk(TokenType::QUESTION_DOT, txt); }
            return tk(TokenType::QUESTION, txt);
        case '/':
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::SLASH_EQ, txt); }
            return tk(TokenType::SLASH, txt);
        case '*':
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::STAR_EQ, txt); }
            return tk(TokenType::STAR, txt);
        case '%':
            if (peek_char() == '=') { txt += advance(); return tk(TokenType::PERCENT_EQ, txt); }
            return tk(TokenType::PERCENT, txt);
        case '~': return tk(TokenType::TILDE, txt);
        case '^': return tk(TokenType::CARET, txt);
        case '(': return tk(TokenType::LPAREN, txt);
        case ')': return tk(TokenType::RPAREN, txt);
        case '{': return tk(TokenType::LBRACE, txt);
        case '}': return tk(TokenType::RBRACE, txt);
        case '[': return tk(TokenType::LBRACKET, txt);
        case ']': return tk(TokenType::RBRACKET, txt);
        case ';': return tk(TokenType::SEMICOLON, txt);
        case ',': return tk(TokenType::COMMA, txt);
        case '.': return tk(TokenType::DOT, txt);
        case ':': return tk(TokenType::COLON, txt);
        default:  return tk(TokenType::IDENTIFIER, txt);
    }
}

// ---------------------------------------------------------------------------
// Regexp context detection (simplified)
// ---------------------------------------------------------------------------

bool Lexer::can_start_regexp() const {
    switch (prev_token_type_) {
        case TokenType::EOF_TOKEN:
        case TokenType::LPAREN:
        case TokenType::LBRACKET:
        case TokenType::LBRACE:
        case TokenType::COMMA:
        case TokenType::SEMICOLON:
        case TokenType::COLON:
        case TokenType::QUESTION:
        case TokenType::TILDE:
        case TokenType::NOT:
        case TokenType::ARROW:
        case TokenType::TEMPLATE_HEAD:
        case TokenType::TEMPLATE_MIDDLE:
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT:
        case TokenType::EQUALS:
        case TokenType::EQ_EQ:
        case TokenType::EQ_EQ_EQ:
        case TokenType::NOT_EQ:
        case TokenType::NOT_EQ_EQ:
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::LT_EQ:
        case TokenType::GT_EQ:
        case TokenType::AMPERSAND:
        case TokenType::PIPE:
        case TokenType::CARET:
        case TokenType::AND_AND:
        case TokenType::OR_OR:
        case TokenType::NULLISH_COALESCING:
        case TokenType::QUESTION_DOT:
        case TokenType::STAR_EQ:
        case TokenType::SLASH_EQ:
        case TokenType::PERCENT_EQ:
        case TokenType::PLUS_EQ:
        case TokenType::MINUS_EQ:
            return true;
        case TokenType::IDENTIFIER:
            return is_regexp_keyword(prev_token_text_);
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Core token reader (shared by next() and peek())
// ---------------------------------------------------------------------------

Token Lexer::read_token_impl() {
    skip_whitespace_and_comments();

    if (eof()) {
        template_state_ = false;
        template_brace_depth_ = 0;
        return make_eof();
    }

    char c = peek_char();

    if (template_state_) {
        if (c == '{') {
            template_brace_depth_++;
        } else if (c == '}' && template_brace_depth_ > 0) {
            template_brace_depth_--;
        } else if (c == '}' && template_brace_depth_ == 0) {
            advance();
            return read_template(TokenType::TEMPLATE_MIDDLE);
        }
    }

    if (c == '"' || c == '\'') {
        return read_string(c);
    }
    if (c == '`') {
        advance();
        return read_template(TokenType::TEMPLATE_HEAD);
    }
    if (is_digit(c) || (c == '.' && is_digit(peek_char(1)))) {
        return read_number();
    }
    if (is_identifier_start(c) || c == '\\') {
        return read_identifier_or_keyword();
    }
    if (c == '/') {
        // /= is always a compound assignment operator, never regexp
        if (peek_char(1) == '=') {
            return read_punctuator();
        }
        if (can_start_regexp()) {
            return read_regexp();
        }
    }
    return read_punctuator();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Token Lexer::next() {
    if (has_lookahead_) {
        has_lookahead_ = false;
        prev_token_type_ = lookahead_.type;
        prev_token_text_ = lookahead_.text;
        return lookahead_;
    }
    Token t = read_token_impl();
    prev_token_type_ = t.type;
    prev_token_text_ = t.text;
    return t;
}

Token Lexer::peek() {
    if (!has_lookahead_) {
        lookahead_ = read_token_impl();
        has_lookahead_ = true;
    }
    return lookahead_;
}

bool Lexer::has_next() {
    if (has_lookahead_) return true;
    skip_whitespace_and_comments();
    return !eof();
}

} // namespace browser::js
