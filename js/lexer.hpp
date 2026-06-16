#pragma once
#include <string>
#include <unordered_map>
#include "token.hpp"

namespace browser::js {

class Lexer {
public:
    Lexer(const std::string& source);
    Token next();
    Token peek();
    bool has_next();

private:
    std::string source_;
    u32 pos_ = 0;
    u32 line_ = 1, column_ = 1;
    Token lookahead_;
    bool has_lookahead_ = false;
    bool template_state_ = false;
    u32 template_brace_depth_ = 0;
    TokenType prev_token_type_ = TokenType::EOF_TOKEN;
    std::string prev_token_text_;

    char peek_char(u32 offset = 0) const;
    char advance();
    bool eof() const;

    void skip_whitespace_and_comments();
    Token read_token_impl();
    Token read_number();
    Token read_string(char quote);
    Token read_template(TokenType head_type);
    Token read_identifier_or_keyword();
    Token read_regexp();
    Token read_punctuator();

    Token make_token(TokenType type, const std::string& text);
    Token make_eof();

    bool can_start_regexp() const;

    static bool is_identifier_start(char c);
    static bool is_identifier_char(char c);
    static bool is_digit(char c);
    static bool is_hex_digit(char c);
    static bool is_oct_digit(char c);
    static bool is_bin_digit(char c);
    static u8 hex_val(char c);
    static bool is_regexp_keyword(const std::string& text);
    static void append_utf8(std::string& s, char32_t cp);
    static void parse_unicode_escape(const std::string& src, u32& pos, std::string& out);
};

} // namespace browser::js
