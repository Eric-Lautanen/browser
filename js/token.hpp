#pragma once
#include <string>
#include <variant>
#include <vector>
#include "../tests/utility.hpp"

namespace browser::js {

enum class TokenType {
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMICOLON, COMMA, DOT, COLON, QUESTION, ARROW,
    PLUS, MINUS, STAR, SLASH, PERCENT, EQUALS,
    EQ_EQ, EQ_EQ_EQ, NOT_EQ, NOT_EQ_EQ,
    LT, GT, LT_EQ, GT_EQ,
    NOT, AMPERSAND, PIPE, CARET, TILDE,
    PLUS_PLUS, MINUS_MINUS, PLUS_EQ, MINUS_EQ,
    STAR_EQ, SLASH_EQ, PERCENT_EQ,
    AND_AND, OR_OR, NULLISH_COALESCING,
    QUESTION_DOT, DOT_DOT_DOT,
    NUMBER, STRING, BOOLEAN, NULL_LITERAL, UNDEFINED, BIGINT, REGEXP,
    IDENTIFIER, INSTANCEOF, TEMPLATE_HEAD, TEMPLATE_MIDDLE, TEMPLATE_TAIL,
    COMMENT, EOF_TOKEN
};

struct Token {
    TokenType type;
    std::string text;
    f64 numeric_value = 0;
    u32 line = 0, column = 0;
};

} // namespace browser::js
