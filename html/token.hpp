#pragma once
#include <string>
#include <vector>
#include <variant>
#include "../tests/utility.hpp"

namespace browser::html {

enum class TokenType { DOCTYPE, START_TAG, END_TAG, COMMENT, CHARACTER, END_OF_FILE };

struct Attribute { std::string name; std::string value; };
struct DoctypeToken { std::string name, public_id, system_id; bool force_quirks = false; };
struct TagToken { TokenType type; std::string tag_name; bool self_closing = false; std::vector<Attribute> attributes; };
struct CommentToken { std::string data; };
struct CharacterToken { char32_t character = 0; };
struct EOFToken {};

using Token = std::variant<DoctypeToken, TagToken, CommentToken, CharacterToken, EOFToken>;

inline TokenType get_type(const Token& token) {
    switch (token.index()) {
        case 0: return TokenType::DOCTYPE;
        case 1: return std::get<TagToken>(token).type;
        case 2: return TokenType::COMMENT;
        case 3: return TokenType::CHARACTER;
        default: return TokenType::END_OF_FILE;
    }
}

inline bool is_tag(const Token& token, const std::string& name = "") {
    if (token.index() != 1) return false;
    auto& tag = std::get<TagToken>(token);
    return name.empty() || tag.tag_name == name;
}

} // namespace browser::html
