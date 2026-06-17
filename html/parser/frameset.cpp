#include "../parser.hpp"

namespace browser::html {

void Parser::handle_in_frameset(const Token& token) {
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c);
            return;
        }
    }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
            if (tag.tag_name == "frameset") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                return;
            }
            if (tag.tag_name == "frame") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                return;
            }
            if (tag.tag_name == "noframes") {
                handle_in_head(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "frameset") {
                if (current_node() && current_node()->tag_name != "frameset") return;
                stack_.pop_back();
                if (current_node() && current_node()->tag_name == "frameset") {
                    mode_ = InsertionMode::IN_FRAMESET;
                } else {
                    mode_ = InsertionMode::AFTER_FRAMESET;
                }
                return;
            }
        }
    }
}

void Parser::handle_after_frameset(const Token& token) {
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c);
            return;
        }
    }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            handle_in_body(token);
            return;
        }
        if (tag.type == TokenType::END_TAG && tag.tag_name == "html") {
            mode_ = InsertionMode::AFTER_AFTER_FRAMESET;
            return;
        }
    }
    if (token.index() == 0) return;
}

} // namespace browser::html
