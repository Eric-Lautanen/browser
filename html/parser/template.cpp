#include "../parser.hpp"

namespace browser::html {

void Parser::handle_in_template(const Token& token) {
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "base" || tag.tag_name == "basefont" || tag.tag_name == "bgsound" ||
                tag.tag_name == "link" || tag.tag_name == "meta" || tag.tag_name == "noframes" ||
                tag.tag_name == "script" || tag.tag_name == "style" || tag.tag_name == "template" ||
                tag.tag_name == "title") {
                handle_in_head(token);
                return;
            }
            if (tag.tag_name == "caption" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "tbody" || tag.tag_name == "td" || tag.tag_name == "tfoot" ||
                tag.tag_name == "th" || tag.tag_name == "thead" || tag.tag_name == "tr") {
                template_modes_.pop_back();
                template_modes_.push_back(InsertionMode::IN_TABLE);
                mode_ = InsertionMode::IN_TABLE;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "col") {
                template_modes_.pop_back();
                template_modes_.push_back(InsertionMode::IN_COLUMN_GROUP);
                mode_ = InsertionMode::IN_COLUMN_GROUP;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "tr") {
                template_modes_.pop_back();
                template_modes_.push_back(InsertionMode::IN_TABLE_BODY);
                mode_ = InsertionMode::IN_TABLE_BODY;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "td" || tag.tag_name == "th") {
                template_modes_.pop_back();
                template_modes_.push_back(InsertionMode::IN_ROW);
                mode_ = InsertionMode::IN_ROW;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "template") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                template_modes_.push_back(InsertionMode::IN_TEMPLATE);
                return;
            }
            template_modes_.pop_back();
            template_modes_.push_back(InsertionMode::IN_BODY);
            mode_ = InsertionMode::IN_BODY;
            handle_token(token);
            return;
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "template") {
                if (!has_element_in_scope("template")) return;
                generate_implied_end_tags();
                while (current_node() && current_node()->tag_name != "template") {
                    stack_.pop_back();
                }
                stack_.pop_back();
                template_modes_.pop_back();
                mode_ = InsertionMode::IN_TEMPLATE;
                return;
            }
            if (tag.tag_name == "html" || tag.tag_name == "body" || tag.tag_name == "caption" ||
                tag.tag_name == "col" || tag.tag_name == "colgroup" || tag.tag_name == "tbody" ||
                tag.tag_name == "td" || tag.tag_name == "tfoot" || tag.tag_name == "th" ||
                tag.tag_name == "thead" || tag.tag_name == "tr") {
                return;
            }
        }
    }
    if (token.index() == 4) {
        if (!template_modes_.empty()) {
            while (current_node() && current_node()->tag_name != "template") {
                stack_.pop_back();
            }
            if (current_node()) stack_.pop_back();
            template_modes_.pop_back();
            if (!template_modes_.empty()) {
                mode_ = template_modes_.back();
            } else {
                mode_ = InsertionMode::IN_BODY;
            }
            handle_token(token);
        }
        return;
    }
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c);
            return;
        }
    }
}

} // namespace browser::html
