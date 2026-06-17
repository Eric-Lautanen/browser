#include "../parser.hpp"
#include "../utf8.hpp"

namespace browser::html {

void Parser::handle_in_body(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == '\0') return;
        flush_pending_text();
        pending_text_ += encode_utf8(c);
        return;
    }
    flush_pending_text();

    if (token.index() == 2) {
        insert_comment(std::get<CommentToken>(token).data);
        return;
    }

    if (token.index() == 0) {
        return;
    }

    if (token.index() != 1) return;
    auto& tag = std::get<TagToken>(token);

    if (tag.type == TokenType::START_TAG) {
        parse_generic_start_tag(tag);
    } else {
        parse_generic_end_tag(tag);
    }
}

void Parser::handle_text(const Token& token) {
    if (token.index() == 4) { // EOF
        stack_.pop_back();
        mode_ = original_mode_;
        handle_token(token);
        return;
    }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::END_TAG) {
            stack_.pop_back();
            mode_ = original_mode_;
            return;
        }
    }
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == '\0') return;
        flush_pending_text();
        pending_text_ += encode_utf8(c);
        return;
    }
}

void Parser::handle_in_table(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c);
            return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "caption") {
                clear_stack_back_to_table_context();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_CAPTION;
                return;
            }
            if (tag.tag_name == "colgroup") {
                clear_stack_back_to_table_context();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_COLUMN_GROUP;
                return;
            }
            if (tag.tag_name == "col") {
                clear_stack_back_to_table_context();
                auto* col_el = create_element_for_token(tag);
                insert_element(col_el);
                mode_ = InsertionMode::IN_COLUMN_GROUP;
                return;
            }
            if (tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead") {
                clear_stack_back_to_table_context();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_TABLE_BODY;
                return;
            }
            if (tag.tag_name == "td" || tag.tag_name == "th" || tag.tag_name == "tr") {
                clear_stack_back_to_table_context();
                TagToken implied_tbody;
                implied_tbody.type = TokenType::START_TAG;
                implied_tbody.tag_name = "tbody";
                auto* tbody_el = create_element_for_token(implied_tbody);
                insert_element(tbody_el);
                stack_.push_back(tbody_el);
                mode_ = InsertionMode::IN_TABLE_BODY;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "table") {
                return;
            }
            if (tag.tag_name == "style" || tag.tag_name == "script" || tag.tag_name == "template") {
                handle_in_head(token);
                return;
            }
            if (tag.tag_name == "input") {
                bool has_type_hidden = false;
                for (const auto& attr : tag.attributes) {
                    if (attr.name == "type" && attr.value == "hidden") {
                        has_type_hidden = true;
                        break;
                    }
                }
                if (has_type_hidden) {
                    auto* el = create_element_for_token(tag);
                    insert_element(el);
                    return;
                }
                foster_parenting_ = true;
                handle_in_body(token);
                foster_parenting_ = false;
                return;
            }
            if (tag.tag_name == "form") {
                foster_parenting_ = true;
                handle_in_body(token);
                foster_parenting_ = false;
                return;
            }
            if (tag.tag_name == "select") {
                reconstruct_active_formatting_elements();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_SELECT_IN_TABLE;
                return;
            }
            foster_parenting_ = true;
            handle_in_body(token);
            foster_parenting_ = false;
            return;
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "table") {
                if (!has_element_in_scope("table")) return;
                while (current_node() && current_node()->tag_name != "table") {
                    stack_.pop_back();
                }
                stack_.pop_back();
                reset_insertion_mode();
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "caption" || tag.tag_name == "col" ||
                tag.tag_name == "colgroup" || tag.tag_name == "html" || tag.tag_name == "tbody" ||
                tag.tag_name == "td" || tag.tag_name == "tfoot" || tag.tag_name == "th" ||
                tag.tag_name == "thead" || tag.tag_name == "tr") {
                return;
            }
        }
    }
    foster_parenting_ = true;
    handle_in_body(token);
    foster_parenting_ = false;
}

void Parser::handle_in_table_body(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            handle_in_body(token);
            return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "tr") {
                clear_stack_back_to_table_body_context();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_ROW;
                return;
            }
            if (tag.tag_name == "th" || tag.tag_name == "td") {
                clear_stack_back_to_table_body_context();
                TagToken implied_tr;
                implied_tr.type = TokenType::START_TAG;
                implied_tr.tag_name = "tr";
                auto* tr_el = create_element_for_token(implied_tr);
                insert_element(tr_el);
                stack_.push_back(tr_el);
                mode_ = InsertionMode::IN_ROW;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "caption" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead") {
                if (!has_element_in_table_scope("tbody") && !has_element_in_table_scope("thead") &&
                    !has_element_in_table_scope("tfoot")) return;
                clear_stack_back_to_table_body_context();
                stack_.pop_back(); // pop tbody/thead/tfoot
                mode_ = InsertionMode::IN_TABLE;
                handle_token(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead") {
                if (!has_element_in_table_scope(tag.tag_name)) return;
                clear_stack_back_to_table_body_context();
                stack_.pop_back();
                mode_ = InsertionMode::IN_TABLE;
                return;
            }
            if (tag.tag_name == "table") {
                if (!has_element_in_table_scope("tbody") && !has_element_in_table_scope("thead") &&
                    !has_element_in_table_scope("tfoot")) return;
                clear_stack_back_to_table_body_context();
                stack_.pop_back(); // pop tbody/thead/tfoot
                mode_ = InsertionMode::IN_TABLE;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "caption" || tag.tag_name == "col" ||
                tag.tag_name == "colgroup" || tag.tag_name == "html" || tag.tag_name == "td" ||
                tag.tag_name == "th" || tag.tag_name == "tr") {
                return;
            }
        }
    }
    handle_in_body(token);
}

void Parser::handle_in_row(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            handle_in_body(token);
            return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "th" || tag.tag_name == "td") {
                clear_stack_back_to_table_row_context();
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                mode_ = InsertionMode::IN_CELL;
                active_formatting_elements_.push_back(nullptr);
                return;
            }
            if (tag.tag_name == "caption" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead" ||
                tag.tag_name == "tr") {
                if (!has_element_in_table_scope("tr")) return;
                clear_stack_back_to_table_row_context();
                stack_.pop_back(); // pop tr
                mode_ = InsertionMode::IN_TABLE_BODY;
                handle_token(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "tr") {
                if (!has_element_in_table_scope("tr")) return;
                clear_stack_back_to_table_row_context();
                stack_.pop_back();
                mode_ = InsertionMode::IN_TABLE_BODY;
                return;
            }
            if (tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead") {
                if (!has_element_in_table_scope(tag.tag_name)) return;
                if (!has_element_in_table_scope("tr")) return;
                clear_stack_back_to_table_row_context();
                stack_.pop_back(); // pop tr
                mode_ = InsertionMode::IN_TABLE_BODY;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "table") {
                if (!has_element_in_table_scope("tr")) return;
                clear_stack_back_to_table_row_context();
                stack_.pop_back(); // pop tr
                mode_ = InsertionMode::IN_TABLE_BODY;
                handle_token(token);
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "caption" || tag.tag_name == "col" ||
                tag.tag_name == "colgroup" || tag.tag_name == "html" || tag.tag_name == "td" ||
                tag.tag_name == "th") {
                return;
            }
        }
    }
    handle_in_body(token);
}

void Parser::handle_in_cell(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == '\0') return;
        insert_character(c);
        return;
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "caption" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "tbody" || tag.tag_name == "tfoot" || tag.tag_name == "thead" ||
                tag.tag_name == "td" || tag.tag_name == "th" || tag.tag_name == "tr") {
                if (!has_element_in_table_scope("td") && !has_element_in_table_scope("th")) return;
                close_cell();
                handle_token(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "td" || tag.tag_name == "th") {
                if (!has_element_in_table_scope(tag.tag_name)) return;
                generate_implied_end_tags();
                while (current_node() && current_node()->tag_name != tag.tag_name) {
                    stack_.pop_back();
                }
                stack_.pop_back();
                if (!active_formatting_elements_.empty() &&
                    active_formatting_elements_.back() == nullptr) {
                    active_formatting_elements_.pop_back();
                }
                reset_insertion_mode();
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "caption" || tag.tag_name == "col" ||
                tag.tag_name == "colgroup" || tag.tag_name == "html") {
                return;
            }
            if (tag.tag_name == "table" || tag.tag_name == "tbody" || tag.tag_name == "tfoot" ||
                tag.tag_name == "thead" || tag.tag_name == "tr") {
                if (!has_element_in_table_scope(tag.tag_name)) return;
                close_cell();
                handle_token(token);
                return;
            }
        }
    }
    handle_in_body(token);
}

void Parser::handle_in_caption(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c);
            return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "caption" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "tbody" || tag.tag_name == "td" || tag.tag_name == "tfoot" ||
                tag.tag_name == "th" || tag.tag_name == "thead" || tag.tag_name == "tr") {
                if (!has_element_in_table_scope("caption")) return;
                generate_implied_end_tags();
                while (current_node() && current_node()->tag_name != "caption") {
                    stack_.pop_back();
                }
                stack_.pop_back();
                mode_ = InsertionMode::IN_TABLE;
                handle_token(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "caption") {
                if (!has_element_in_table_scope("caption")) return;
                generate_implied_end_tags();
                while (current_node() && current_node()->tag_name != "caption") {
                    stack_.pop_back();
                }
                stack_.pop_back();
                mode_ = InsertionMode::IN_TABLE;
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "col" || tag.tag_name == "colgroup" ||
                tag.tag_name == "html" || tag.tag_name == "tbody" || tag.tag_name == "td" ||
                tag.tag_name == "tfoot" || tag.tag_name == "th" || tag.tag_name == "thead" ||
                tag.tag_name == "tr") {
                return;
            }
        }
    }
    handle_in_body(token);
}

void Parser::handle_in_column_group(const Token& token) {
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
            if (tag.tag_name == "col") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                return;
            }
            if (tag.tag_name == "template") {
                handle_in_template(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "colgroup") {
                if (!current_node() || current_node()->tag_name != "colgroup") return;
                stack_.pop_back();
                mode_ = InsertionMode::IN_TABLE;
                return;
            }
            if (tag.tag_name == "col") {
                return;
            }
            if (tag.tag_name == "template") {
                handle_in_template(token);
                return;
            }
        }
    }
    if (!current_node() || current_node()->tag_name != "colgroup") return;
    stack_.pop_back();
    mode_ = InsertionMode::IN_TABLE;
    handle_token(token);
}

void Parser::handle_in_select(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == '\0') return;
        insert_character(c);
        return;
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 0) return;

    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "option") {
                if (current_node() && current_node()->tag_name == "option") {
                    stack_.pop_back();
                }
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                return;
            }
            if (tag.tag_name == "optgroup") {
                if (current_node() && current_node()->tag_name == "option") {
                    stack_.pop_back();
                }
                if (current_node() && current_node()->tag_name == "optgroup") {
                    stack_.pop_back();
                }
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                return;
            }
            if (tag.tag_name == "hr") {
                if (current_node() && current_node()->tag_name == "option") {
                    stack_.pop_back();
                }
                if (current_node() && current_node()->tag_name == "optgroup") {
                    stack_.pop_back();
                }
                auto* el = create_element_for_token(tag);
                insert_element(el);
                return;
            }
            if (tag.tag_name == "select") {
                return;
            }
            if (tag.tag_name == "input" || tag.tag_name == "textarea") {
                if (mode_ == InsertionMode::IN_SELECT_IN_TABLE) {
                    if (!has_element_in_select_scope("select")) return;
                }
                for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                    if (stack_[i] && stack_[i]->tag_name == "select") {
                        stack_.resize(static_cast<u32>(i));
                        break;
                    }
                }
                reset_insertion_mode();
                handle_token(token);
                return;
            }
            if (tag.tag_name == "template") {
                handle_in_template(token);
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "option") {
                if (current_node() && current_node()->tag_name == "option") {
                    stack_.pop_back();
                }
                return;
            }
            if (tag.tag_name == "optgroup") {
                if (current_node() && current_node()->tag_name == "option") {
                    stack_.pop_back();
                }
                if (current_node() && current_node()->tag_name == "optgroup") {
                    stack_.pop_back();
                }
                return;
            }
            if (tag.tag_name == "select") {
                if (!has_element_in_select_scope("select")) return;
                for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                    if (stack_[i] && stack_[i]->tag_name == "select") {
                        stack_.resize(static_cast<u32>(i));
                        break;
                    }
                }
                reset_insertion_mode();
                return;
            }
            if (tag.tag_name == "template") {
                handle_in_template(token);
                return;
            }
        }
    }
}

void Parser::close_cell() {
    generate_implied_end_tags();
    while (current_node() && current_node()->tag_name != "td" && current_node()->tag_name != "th") {
        stack_.pop_back();
    }
    if (current_node()) {
        stack_.pop_back();
    }
    if (!active_formatting_elements_.empty() &&
        active_formatting_elements_.back() == nullptr) {
        active_formatting_elements_.pop_back();
    }
    reset_insertion_mode();
}

} // namespace browser::html
