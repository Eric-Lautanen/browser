#pragma once
#include <string>
#include "../tests/utility.hpp"

namespace browser::css {

enum class CssTokenType {
    IDENT, FUNCTION, AT_KEYWORD, HASH, STRING, URL, NUMBER,
    PERCENTAGE, DIMENSION, WHITESPACE, COLON, SEMICOLON, COMMA,
    OPEN_BRACE, CLOSE_BRACE, OPEN_PAREN, CLOSE_PAREN,
    OPEN_BRACKET, CLOSE_BRACKET, DELIM, EOF_TOKEN
};

struct CssToken {
    CssTokenType type;
    std::string text;
    f32 numeric_value = 0;
};

class CssTokenizer {
public:
    CssTokenizer(const std::string& input);
    CssToken next();
    bool has_next() const;

private:
    std::string input_;
    u32 pos_ = 0;

    char peek(u32 offset = 0) const;
    char advance();
    bool is_eof() const;
    static bool is_whitespace(char c);
    static bool is_digit(char c);
    static bool is_hex_digit(char c);
    static bool is_ident_start(char c);
    static bool is_name_char(char c);
    bool starts_ident() const;
    std::string consume_escape();
    std::string consume_ident();
    CssToken consume_numeric();
    CssToken consume_string(char quote);
    CssToken consume_ident_like();
    CssToken consume_url();
    CssToken consume_hash();
    CssToken consume_at_keyword();
    void consume_comment();
};

}
