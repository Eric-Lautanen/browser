#include "../parser.hpp"

namespace browser::html {

    void Parser::handle_after_body(const Token &token) {
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                handle_in_body(token);
                return;
            }
        }
        if (token.index() == 2) {
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
            if (tag.type == TokenType::END_TAG && tag.tag_name == "html") {
                mode_ = InsertionMode::AFTER_AFTER_BODY;
                return;
            }
        }
        // Spec ("after body"): anything else is a parse error — switch to
        // "in body" and reprocess so content still lands inside <body>.
        mode_ = InsertionMode::IN_BODY;
        handle_token(token);
    }

    void Parser::handle_after_after_body(const Token &token) {
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                handle_in_body(token);
                return;
            }
        }
        if (token.index() == 2) {
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
        }
        // Spec ("after after body"): anything else — switch to "in body" and reprocess.
        mode_ = InsertionMode::IN_BODY;
        handle_token(token);
    }

    void Parser::handle_after_after_frameset(const Token &token) {
        if (token.index() == 2) {
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
        }
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                insert_character(c);
                return;
            }
        }
        if (token.index() == 0)
            return;
    }

}  // namespace browser::html
