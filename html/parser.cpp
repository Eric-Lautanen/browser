#include "parser.hpp"
#include "utf8.hpp"
#include <unordered_set>

namespace browser::html {

Parser::Parser() : preload_scanner_(nullptr) {}

Parser::Parser(PreloadScanner* scanner, const std::string& base_url)
    : preload_scanner_(scanner), base_url_(base_url) {}

std::unique_ptr<Document> Parser::parse(const std::string& html) {
    document_ = create_document();
    tokenizer_ = std::make_unique<Tokenizer>();
    tokenizer_->feed(html);
    tokenizer_->end();

    while (tokenizer_->has_next()) {
        Token t = tokenizer_->next();
        if (preload_scanner_) {
            preload_scanner_->scan_token(t, base_url_);
        }
        handle_token(t);
    }
    flush_pending_text();

    // Implicitly close whatever remains open
    return std::move(document_);
}

Element* Parser::current_node() const {
    return stack_.empty() ? nullptr : stack_.back();
}

void Parser::handle_token(const Token& token) {
    switch (mode_) {
        case InsertionMode::INITIAL: handle_initial(token); break;
        case InsertionMode::BEFORE_HTML: handle_before_html(token); break;
        case InsertionMode::BEFORE_HEAD: handle_before_head(token); break;
        case InsertionMode::IN_HEAD: handle_in_head(token); break;
        case InsertionMode::AFTER_HEAD: handle_after_head(token); break;
        case InsertionMode::IN_BODY: handle_in_body(token); break;
        case InsertionMode::TEXT: handle_text(token); break;
        case InsertionMode::IN_TABLE: handle_in_table(token); break;
        case InsertionMode::IN_TABLE_BODY: handle_in_table_body(token); break;
        case InsertionMode::IN_ROW: handle_in_row(token); break;
        case InsertionMode::IN_CELL: handle_in_cell(token); break;
        case InsertionMode::IN_SELECT: handle_in_select(token); break;
        case InsertionMode::IN_SELECT_IN_TABLE: handle_in_select(token); break;
        case InsertionMode::IN_CAPTION: handle_in_caption(token); break;
        case InsertionMode::IN_COLUMN_GROUP: handle_in_column_group(token); break;
        case InsertionMode::IN_TEMPLATE: handle_in_template(token); break;
        case InsertionMode::IN_FRAMESET: handle_in_frameset(token); break;
        case InsertionMode::AFTER_FRAMESET: handle_after_frameset(token); break;
        case InsertionMode::AFTER_AFTER_FRAMESET: handle_after_after_frameset(token); break;
        case InsertionMode::AFTER_BODY: handle_after_body(token); break;
        case InsertionMode::AFTER_AFTER_BODY: handle_after_after_body(token); break;
        default: break;
    }
}

// --- Insertion mode handlers ---

void Parser::handle_initial(const Token& token) {
    if (token.index() == 0) { // DOCTYPE
        auto& dt = std::get<DoctypeToken>(token);
        insert_doctype(dt);
        if (dt.force_quirks || dt.name != "html") {
            // quirks mode
        }
        mode_ = InsertionMode::BEFORE_HTML;
        return;
    }
    if (token.index() == 3) { // CHARACTER (whitespace)

        return;
    }
    if (token.index() == 2) { // COMMENT
        insert_comment(std::get<CommentToken>(token).data);
        return;
    }
    // Anything else: implied <html>
    mode_ = InsertionMode::BEFORE_HTML;
    handle_token(token);
}

void Parser::handle_before_html(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') return;
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            auto* html = create_element_for_token(tag);
            append_child(document_.get(), std::unique_ptr<Node>(html));
            stack_.push_back(html);
            mode_ = InsertionMode::BEFORE_HEAD;
            return;
        }
    }
    // Implied <html>
    TagToken implied;
    implied.type = TokenType::START_TAG;
    implied.tag_name = "html";
    auto* html = create_element_for_token(implied);
    append_child(document_.get(), std::unique_ptr<Node>(html));
    stack_.push_back(html);
    mode_ = InsertionMode::BEFORE_HEAD;
    handle_token(token);
}

void Parser::handle_before_head(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') return;
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            handle_in_body(token);
            return;
        }
        if (tag.type == TokenType::START_TAG && tag.tag_name == "head") {
            auto* head = create_element_for_token(tag);
            insert_element(head);
            stack_.push_back(head);
            head_element_pointer_ = head;
            mode_ = InsertionMode::IN_HEAD;
            return;
        }
    }
    // Implied <head>
    TagToken implied;
    implied.type = TokenType::START_TAG;
    implied.tag_name = "head";
    auto* head = create_element_for_token(implied);
    insert_element(head);
    stack_.push_back(head);
    head_element_pointer_ = head;
    mode_ = InsertionMode::IN_HEAD;
    handle_token(token);
}

void Parser::handle_in_head(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c); return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
            if (tag.tag_name == "meta" || tag.tag_name == "base" || tag.tag_name == "basefont" ||
                tag.tag_name == "bgsound" || tag.tag_name == "link" || tag.tag_name == "command") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                stack_.pop_back();
                return;
            }
            if (tag.tag_name == "title") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                tokenizer_->set_appropriate_end_tag("title");
        tokenizer_->set_state(1); // RCDATA
                original_mode_ = mode_;
                mode_ = InsertionMode::TEXT;
                return;
            }
            if (tag.tag_name == "style" || tag.tag_name == "noframes") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                tokenizer_->set_appropriate_end_tag(tag.tag_name);
                tokenizer_->set_state(2); // RAWTEXT
                original_mode_ = mode_;
                mode_ = InsertionMode::TEXT;
                return;
            }
            if (tag.tag_name == "noscript") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                tokenizer_->set_appropriate_end_tag("noscript");
                tokenizer_->set_state(2); // RAWTEXT
                original_mode_ = mode_;
                mode_ = InsertionMode::TEXT;
                return;
            }
            if (tag.tag_name == "script") {
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                tokenizer_->set_appropriate_end_tag("script");
        tokenizer_->set_state(4); // SCRIPT_DATA
                original_mode_ = mode_;
                mode_ = InsertionMode::TEXT;
                return;
            }
            if (tag.tag_name == "head") {
                // parse error, ignore
                return;
            }
        }
        if (tag.type == TokenType::END_TAG) {
            if (tag.tag_name == "head") {
                if (!stack_.empty()) stack_.pop_back();
                head_element_pointer_ = nullptr;
                mode_ = InsertionMode::AFTER_HEAD;
                return;
            }
            if (tag.tag_name == "body" || tag.tag_name == "html" || tag.tag_name == "br") {
                // pop head, switch mode, reprocess token
                if (!stack_.empty()) stack_.pop_back();
                head_element_pointer_ = nullptr;
                mode_ = InsertionMode::AFTER_HEAD;
                handle_token(token);
                return;
            }
        }
    }
    // Anything else: pop head, switch to AFTER_HEAD, reprocess
    if (!stack_.empty()) stack_.pop_back();
    head_element_pointer_ = nullptr;
    mode_ = InsertionMode::AFTER_HEAD;
    handle_token(token);
}

void Parser::handle_after_head(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            insert_character(c); return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG) {
            if (tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
            if (tag.tag_name == "body") {
                auto* body = create_element_for_token(tag);
                insert_element(body);
                stack_.push_back(body);
                frameset_ok_ = false;
                mode_ = InsertionMode::IN_BODY;
                return;
            }
            if (tag.tag_name == "frameset") {
                auto* fs = create_element_for_token(tag);
                insert_element(fs);
                stack_.push_back(fs);
                mode_ = InsertionMode::AFTER_HEAD_FRAMESET;
                return;
            }
            if (tag.tag_name == "base" || tag.tag_name == "basefont" || tag.tag_name == "bgsound" ||
                tag.tag_name == "link" || tag.tag_name == "meta" || tag.tag_name == "noframes" ||
                tag.tag_name == "script" || tag.tag_name == "style" || tag.tag_name == "title" ||
                tag.tag_name == "noscript") {
                // These should be handled in IN_HEAD context. Create a dummy head on the stack.
                TagToken head_implied;
                head_implied.type = TokenType::START_TAG;
                head_implied.tag_name = "head";
                auto* head_stub = create_element_for_token(head_implied);
                insert_element(head_stub);
                stack_.push_back(head_stub);
                head_element_pointer_ = head_stub;
                mode_ = InsertionMode::IN_HEAD;
                handle_token(token);
                // Pop the stub head; it stays in the tree as a child of html
                head_element_pointer_ = nullptr;
                stack_.pop_back();
                mode_ = InsertionMode::AFTER_HEAD;
                return;
            }
        }
        if (tag.type == TokenType::END_TAG && tag.tag_name == "template") {
            return;
        }
    }
    // Anything else: implied <body>
    TagToken implied;
    implied.type = TokenType::START_TAG;
    implied.tag_name = "body";
    auto* body = create_element_for_token(implied);
    insert_element(body);
    stack_.push_back(body);
    mode_ = InsertionMode::IN_BODY;
    handle_token(token);
}

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
        // DOCTYPE in body: parse error, ignore
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
    // TEXT mode is used for RCDATA/rawtext/script/plaintext elements
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
    if (token.index() == 0) return; // ignore doctype in table

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
                // col is a void element, pop immediately
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
                // parse error, ignore
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
                // parse error, ignore (foster parent)
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

void Parser::handle_after_body(const Token& token) {
    if (token.index() == 3) {
        char32_t c = std::get<CharacterToken>(token).character;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
            handle_in_body(token);
            return;
        }
    }
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            handle_in_body(token);
            return;
        }
        if (tag.type == TokenType::END_TAG && tag.tag_name == "html") {
            mode_ = InsertionMode::AFTER_AFTER_BODY;
            return;
        }
    }
    // parse error, ignore
}

void Parser::handle_after_after_body(const Token& token) {
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
        if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
            handle_in_body(token);
            return;
        }
    }
    // parse error, ignore
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
                // parse error, ignore
                return;
            }
            if (tag.tag_name == "input" || tag.tag_name == "textarea") {
                // parse error
                if (mode_ == InsertionMode::IN_SELECT_IN_TABLE) {
                    if (!has_element_in_select_scope("select")) return;
                }
                // Close select element
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
                // void element
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
                // push template onto stack, push mode
                auto* el = create_element_for_token(tag);
                insert_element(el);
                stack_.push_back(el);
                template_modes_.push_back(InsertionMode::IN_TEMPLATE);
                return;
            }
            // Anything else
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
            // pop template, reparse
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
    // Anything else: parse error, ignore
}

void Parser::handle_after_after_frameset(const Token& token) {
    if (token.index() == 2) { insert_comment(std::get<CommentToken>(token).data); return; }
    if (token.index() == 1) {
        auto& tag = std::get<TagToken>(token);
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
    if (token.index() == 0) return;
    // Parse error, ignore
}

void Parser::reset_insertion_mode() {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        auto* node = stack_[static_cast<u32>(i)];
        if (!node) continue;
        const std::string& t = node->tag_name;
        if (t == "select") {
            mode_ = InsertionMode::IN_SELECT;
            return;
        }
        if (t == "td" || t == "th") {
            mode_ = InsertionMode::IN_CELL;
            return;
        }
        if (t == "tr") {
            mode_ = InsertionMode::IN_ROW;
            return;
        }
        if (t == "tbody" || t == "thead" || t == "tfoot") {
            mode_ = InsertionMode::IN_TABLE_BODY;
            return;
        }
        if (t == "caption") {
            mode_ = InsertionMode::IN_CAPTION;
            return;
        }
        if (t == "colgroup") {
            mode_ = InsertionMode::IN_COLUMN_GROUP;
            return;
        }
        if (t == "table") {
            mode_ = InsertionMode::IN_TABLE;
            return;
        }
        if (t == "template") {
            if (!template_modes_.empty()) {
                mode_ = template_modes_.back();
            } else {
                mode_ = InsertionMode::IN_BODY;
            }
            return;
        }
        if (t == "head") {
            if (t == "head") {
                // Only if head is still open
                mode_ = InsertionMode::IN_HEAD;
                return;
            }
        }
        if (t == "body") {
            mode_ = InsertionMode::IN_BODY;
            return;
        }
        if (t == "frameset") {
            mode_ = InsertionMode::IN_FRAMESET;
            return;
        }
        if (t == "noframes") {
            mode_ = InsertionMode::IN_BODY;
            return;
        }
        if (t == "html") {
            if (head_element_pointer_ == nullptr) {
                mode_ = InsertionMode::BEFORE_HEAD;
            } else {
                mode_ = InsertionMode::AFTER_HEAD;
            }
            return;
        }
    }
    mode_ = InsertionMode::IN_BODY;
}

// --- Helper methods ---

void Parser::insert_element(Element* element) {
    if (foster_parenting_) {
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[i] && stack_[i]->tag_name == "table") {
                Node* table_parent = stack_[i]->parent;
                auto& siblings = table_parent->children;
                auto it = siblings.begin();
                for (; it != siblings.end(); ++it) {
                    if (it->get() == stack_[i]) break;
                }
                // insert BEFORE the table (foster parenting per spec)
                element->parent = table_parent;
                siblings.insert(it, std::unique_ptr<Node>(static_cast<Node*>(element)));
                return;
            }
        }
    }
    if (current_node()) {
        append_child(current_node(), std::unique_ptr<Node>(static_cast<Node*>(element)));
    }
}

void Parser::insert_character(char32_t c) {
    flush_pending_text();
    pending_text_ += encode_utf8(c);
}

void Parser::flush_pending_text() {
    if (pending_text_.empty()) return;
    if (foster_parenting_) {
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[i] && stack_[i]->tag_name == "table") {
                auto text_node = create_text(pending_text_);
                pending_text_.clear();
                Node* table_parent = stack_[i]->parent;
                if (!table_parent) break;
                auto& siblings = table_parent->children;
                auto it = siblings.begin();
                for (; it != siblings.end(); ++it) {
                    if (it->get() == stack_[i]) break;
                }
                text_node->parent = table_parent;
                siblings.insert(it, std::move(text_node));
                return;
            }
        }
        // No table found or table has no parent — fall through to normal flush
    }
    if (current_node()) {
        // Check if last child is already a Text node; coalesce
        if (!current_node()->children.empty()) {
            auto& last = current_node()->children.back();
            if (last->type == NodeType::TEXT) {
                auto* text_node = static_cast<Text*>(last.get());
                text_node->data += pending_text_;
                pending_text_.clear();
                return;
            }
        }
        append_child(current_node(), create_text(pending_text_));
    }
    pending_text_.clear();
}

void Parser::insert_comment(const std::string& data) {
    auto c = std::make_unique<Comment>();
    c->data = data;
    if (current_node()) {
        append_child(current_node(), std::move(c));
    }
}

void Parser::insert_doctype(const DoctypeToken& token) {
    auto dt = std::make_unique<DocumentType>();
    dt->name = token.name;
    dt->public_id = token.public_id;
    dt->system_id = token.system_id;
    dt->force_quirks = token.force_quirks;
    append_child(document_.get(), std::move(dt));
}

Element* Parser::create_element_for_token(const TagToken& token) {
    auto el = std::make_unique<Element>(token.tag_name);
    for (const auto& attr : token.attributes) {
        el->attributes[attr.name] = attr.value;
    }
    // The tree owns the element; return raw pointer
    return el.release();
}

void Parser::generate_implied_end_tags(const std::vector<std::string>& exceptions) {
    static const std::unordered_set<std::string> implied_tags = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"
    };
    while (current_node()) {
        std::string tag = current_node()->tag_name;
        if (implied_tags.find(tag) != implied_tags.end()) {
            bool is_exception = false;
            for (const auto& e : exceptions) {
                if (tag == e) { is_exception = true; break; }
            }
            if (is_exception) break;
            stack_.pop_back();
        } else {
            break;
        }
    }
}

bool Parser::has_element_in_scope(const std::string& tag_name, const std::vector<std::string>& extras) {
    static const std::unordered_set<std::string> scope_tags = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (scope_tags.find(stack_[i]->tag_name) != scope_tags.end()) return false;
        for (const auto& e : extras) {
            if (stack_[i]->tag_name == e) return false;
        }
    }
    return false;
}

bool Parser::has_element_in_scope(const std::vector<std::string>& tags) {
    for (const auto& tag : tags) {
        if (has_element_in_scope(tag)) return true;
    }
    return false;
}

void Parser::close_element(const std::string& tag_name) {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (stack_[i] && stack_[i]->tag_name == tag_name) {
            stack_.resize(static_cast<u32>(i));
            return;
        }
    }
}

bool Parser::is_special_tag(const std::string& tag) {
    static const std::unordered_set<std::string> special = {
        "address", "applet", "area", "article", "aside", "base", "basefont", "bgsound",
        "blockquote", "body", "br", "button", "caption", "center", "col", "colgroup",
        "dd", "details", "dir", "div", "dl", "dt", "embed", "fieldset", "figcaption",
        "figure", "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6",
        "head", "header", "hgroup", "hr", "html", "iframe", "img", "input", "isindex",
        "li", "link", "listing", "main", "marquee", "menu", "meta", "nav", "noembed",
        "noframes", "noscript", "object", "ol", "p", "param", "plaintext", "pre",
        "script", "section", "select", "source", "style", "summary", "table",
        "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "title",
        "tr", "track", "ul", "wbr", "xmp"
    };
    return special.find(tag) != special.end();
}

bool is_void_element(const std::string& tag) {
    static const std::unordered_set<std::string> voids = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    return voids.find(tag) != voids.end();
}

bool Parser::is_heading_tag(const std::string& tag) {
    return tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6';
}

void Parser::parse_generic_start_tag(const TagToken& tag) {
    const std::string& t = tag.tag_name;

    // --- Special handling for specific tags ---
    if (t == "html") {
        // If there's already an html element, merge attributes
        if (!stack_.empty() && stack_[0]->tag_name == "html") {
            for (const auto& attr : tag.attributes) {
                if (stack_[0]->attributes.find(attr.name) == stack_[0]->attributes.end()) {
                    stack_[0]->attributes[attr.name] = attr.value;
                }
            }
            return;
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "base" || t == "basefont" || t == "bgsound" || t == "link" || t == "meta") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        // void element - pop immediately
        return;
    }

    if (t == "script") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("script");
        tokenizer_->set_state(4); // SCRIPT_DATA
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "style" || t == "xmp" || t == "iframe" || t == "noembed" || t == "noframes") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag(t);
        tokenizer_->set_state(2); // RAWTEXT
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "title" || t == "textarea") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag(t);
        tokenizer_->set_state(1); // RCDATA
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }
    if (t == "noscript") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("noscript");
        tokenizer_->set_state(2); // RAWTEXT
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        return;
    }

    if (t == "body") {
        if (stack_.size() < 2 || stack_[1]->tag_name != "body") return;
        frameset_ok_ = false;
        for (const auto& attr : tag.attributes) {
            if (stack_[1]->attributes.find(attr.name) == stack_[1]->attributes.end()) {
                stack_[1]->attributes[attr.name] = attr.value;
            }
        }
        return;
    }

    if (t == "frameset") {
        if (!frameset_ok_) return;
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        mode_ = InsertionMode::AFTER_HEAD_FRAMESET;
        return;
    }

    // --- Elements that auto-close <p> ---
    if (t == "address" || t == "article" || t == "aside" || t == "blockquote" || t == "center" ||
        t == "details" || t == "dialog" || t == "dir" || t == "div" || t == "dl" || t == "fieldset" ||
        t == "figcaption" || t == "figure" || t == "footer" || t == "header" || t == "hgroup" ||
        t == "main" || t == "nav" || t == "ol" || t == "p" || t == "section" || t == "summary" ||
        t == "ul") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "h1" || t == "h2" || t == "h3" || t == "h4" || t == "h5" || t == "h6") {
        if (has_element_in_scope("p")) close_element("p");
        // Check if current node is a heading (h1-h6), if so close it
        if (current_node() && is_heading_tag(current_node()->tag_name)) {
            stack_.pop_back();
        }
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "textarea") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        tokenizer_->set_appropriate_end_tag("textarea");
        original_mode_ = mode_;
        mode_ = InsertionMode::TEXT;
        frameset_ok_ = false;
        return;
    }

    if (t == "pre" || t == "listing") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        return;
    }

    if (t == "form") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        return;
    }

    if (t == "select") {
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        mode_ = InsertionMode::IN_SELECT;
        frameset_ok_ = false;
        return;
    }

    if (t == "li") {
        frameset_ok_ = false;
        // Close any open li, then implied end tags
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[i] && stack_[i]->tag_name == "li") {
                stack_.resize(static_cast<u32>(i));
                break;
            }
        }
        generate_implied_end_tags({"li"});
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "dd" || t == "dt") {
        frameset_ok_ = false;
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[i] && (stack_[i]->tag_name == "dd" || stack_[i]->tag_name == "dt")) {
                stack_.resize(static_cast<u32>(i));
                break;
            }
        }
        generate_implied_end_tags({"dd", "dt"});
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "template") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        template_modes_.push_back(InsertionMode::IN_TEMPLATE);
        frameset_ok_ = false;
        return;
    }

    if (t == "plaintext") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "a") {
        // If there's an active "a" element, close it using adoption agency
        for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
            if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
            if (active_formatting_elements_[static_cast<u32>(i)] &&
                active_formatting_elements_[static_cast<u32>(i)]->tag_name == "a") {
                adoption_agency_algorithm("a");
                break;
            }
        }
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "b" || t == "big" || t == "code" || t == "em" || t == "font" || t == "i" ||
        t == "s" || t == "small" || t == "strike" || t == "strong" || t == "tt" || t == "u") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "nobr") {
        // If there's already a nobr in scope, use adoption agency first
        if (has_element_in_scope("nobr")) {
            adoption_agency_algorithm("nobr");
        }
        reconstruct_active_formatting_elements();
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        push_active_formatting_element(el);
        return;
    }

    if (t == "br") {
        // br is a void element
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        stack_.pop_back();
        frameset_ok_ = false;
        return;
    }

    if (t == "p") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    if (t == "span") {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    // Void elements
    if (is_void_element(t)) {
        auto* el = create_element_for_token(tag);
        insert_element(el);
        if (t == "input" || t == "hr" || t == "img" || t == "embed" || t == "wbr") {
            // Do nothing special, just insert
        }
        if (t == "br") frameset_ok_ = false;
        return;
    }

    // --- Table elements ---
    if (t == "table") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        frameset_ok_ = false;
        mode_ = InsertionMode::IN_TABLE;
        return;
    }

    // caption, col, colgroup, tbody, td, tfoot, th, thead, tr
    // are handled in their respective insertion modes only

    // --- Image (legacy) ---
    if (t == "image") {
        TagToken img = tag;
        img.tag_name = "img";
        parse_generic_start_tag(img);
        return;
    }

    // --- HR ---
    if (t == "hr") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        // void
        frameset_ok_ = false;
        return;
    }

    // --- Lists ---
    if (t == "ol" || t == "ul") {
        if (has_element_in_scope("p")) close_element("p");
        auto* el = create_element_for_token(tag);
        insert_element(el);
        stack_.push_back(el);
        return;
    }

    // --- Generic start tag ---
    auto* el = create_element_for_token(tag);
    insert_element(el);
    if (!is_void_element(t)) {
        stack_.push_back(el);
    }
}

void Parser::parse_generic_end_tag(const TagToken& tag) {
    const std::string& t = tag.tag_name;

    // --- Special end tag handling ---

    if (t == "body") {
        if (!has_element_in_scope("body")) return;
        mode_ = InsertionMode::AFTER_BODY;
        return;
    }

    if (t == "html") {
        if (!has_element_in_scope("body")) return;
        mode_ = InsertionMode::AFTER_BODY;
        return;
    }

    if (t == "br") {
        // </br> is a parse error; treat as <br>
        TagToken start = tag;
        start.type = TokenType::START_TAG;
        parse_generic_start_tag(start);
        return;
    }

    if (t == "p") {
        if (!has_element_in_scope("p")) return;
        generate_implied_end_tags({"p"});
        close_element("p");
        return;
    }

    if (t == "li") {
        if (!has_element_in_scope("li")) return;
        generate_implied_end_tags({"li"});
        close_element("li");
        return;
    }

    if (t == "dd" || t == "dt") {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags({t});
        close_element(t);
        return;
    }

    if (is_heading_tag(t)) {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags();
        close_element(t);
        return;
    }

    if (t == "a" || t == "b" || t == "big" || t == "code" || t == "em" || t == "font" ||
        t == "i" || t == "nobr" || t == "s" || t == "small" || t == "strike" ||
        t == "strong" || t == "tt" || t == "u") {
        // Use adoption agency algorithm for formatting elements
        adoption_agency_algorithm(t);
        return;
    }

    if (t == "div" || t == "section" || t == "nav" || t == "article" || t == "aside" ||
        t == "header" || t == "footer" || t == "main" || t == "address" || t == "blockquote" ||
        t == "center" || t == "details" || t == "dialog" || t == "dir" || t == "dl" ||
        t == "fieldset" || t == "figcaption" || t == "figure" || t == "hgroup" ||
        t == "listing" || t == "menu" || t == "pre" || t == "summary" || t == "ul" ||
        t == "ol") {
        if (!has_element_in_scope(t)) return;
        generate_implied_end_tags({t});
        close_element(t);
        return;
    }

    // Generic end tag: walk up and close
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (stack_[i] && stack_[i]->tag_name == t) {
            generate_implied_end_tags({t});
            stack_.resize(static_cast<u32>(i));
            return;
        }
    }
}

bool Parser::has_element_in_list_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> list_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title", "ol", "ul"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (list_scope.find(stack_[i]->tag_name) != list_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_button_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> button_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title", "button"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (button_scope.find(stack_[i]->tag_name) != button_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_table_scope(const std::string& tag_name) {
    static const std::unordered_set<std::string> table_scope = {
        "applet", "caption", "html", "table", "td", "th", "marquee", "object",
        "template", "mi", "mo", "mn", "ms", "mtext", "annotation-xml",
        "foreignobject", "desc", "title"
    };
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (table_scope.find(stack_[i]->tag_name) != table_scope.end()) return false;
    }
    return false;
}

bool Parser::has_element_in_select_scope(const std::string& tag_name) {
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (!stack_[i]) continue;
        if (stack_[i]->tag_name == tag_name) return true;
        if (stack_[i]->tag_name == "select") return false;
    }
    return false;
}

void Parser::clear_stack_back_to_table_context() {
    while (current_node() && current_node()->tag_name != "table" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

void Parser::clear_stack_back_to_table_body_context() {
    while (current_node() && current_node()->tag_name != "tbody" &&
           current_node()->tag_name != "tfoot" && current_node()->tag_name != "thead" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

void Parser::clear_stack_back_to_table_row_context() {
    while (current_node() && current_node()->tag_name != "tr" &&
           current_node()->tag_name != "template" && current_node()->tag_name != "html") {
        stack_.pop_back();
    }
}

void Parser::push_active_formatting_element(Element* el) {
    // Remove any existing identical elements beyond the last marker
    int count = 0;
    for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
        if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
        if (active_formatting_elements_[static_cast<u32>(i)] == el ||
            (active_formatting_elements_[static_cast<u32>(i)] &&
             active_formatting_elements_[static_cast<u32>(i)]->tag_name == el->tag_name)) {
            count++;
        }
        if (count >= 3) {
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + static_cast<i64>(i));
            break;
        }
    }
    active_formatting_elements_.push_back(el);
}

void Parser::remove_active_formatting_element(Element* el) {
    for (auto it = active_formatting_elements_.begin(); it != active_formatting_elements_.end(); ++it) {
        if (*it == el) {
            active_formatting_elements_.erase(it);
            return;
        }
    }
}

int Parser::position_in_active_formatting_list(Element* el) {
    for (u32 i = 0; i < active_formatting_elements_.size(); i++) {
        if (active_formatting_elements_[i] == el) return static_cast<i32>(i);
    }
    return -1;
}

void Parser::reconstruct_active_formatting_elements() {
    if (active_formatting_elements_.empty()) return;

    // Find the last non-marker entry, or the last entry if none exists
    i32 entry = static_cast<i32>(active_formatting_elements_.size()) - 1;
    if (entry < 0) return;

    // If last entry is a marker or null, do nothing
    if (active_formatting_elements_[static_cast<u32>(entry)] == nullptr) return;

    // Check if entry is already in the stack
    if (entry >= 0 && active_formatting_elements_[static_cast<u32>(entry)]) {
        Element* el = active_formatting_elements_[static_cast<u32>(entry)];
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] == el) return;
        }
    }

    i32 pos = -1;
    for (i32 i = entry; i >= 0; i--) {
        if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) {
            pos = i + 1;
            break;
        }
        Element* e = active_formatting_elements_[static_cast<u32>(i)];
        bool found = false;
        for (u32 j = 0; j < stack_.size(); j++) {
            if (stack_[j] == e) { found = true; break; }
        }
        if (found) {
            pos = i;
            break;
        }
        pos = i;
    }
    if (pos < 0) return;

    for (i32 i = pos; i <= entry; i++) {
        Element* e = active_formatting_elements_[static_cast<u32>(i)];
        if (e == nullptr) continue;

        TagToken clone_tok;
        clone_tok.type = TokenType::START_TAG;
        clone_tok.tag_name = e->tag_name;
        auto* clone = create_element_for_token(clone_tok);
        for (const auto& [k, v] : e->attributes) {
            clone->attributes[k] = v;
        }
        insert_element(clone);
        stack_.push_back(clone);
        active_formatting_elements_[static_cast<u32>(i)] = clone;
    }
}

void Parser::adoption_agency_algorithm(const std::string& subject) {
    if (active_formatting_elements_.empty()) {
        // Simple fallback: just close element
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
                generate_implied_end_tags({subject});
                stack_.resize(static_cast<u32>(i));
                return;
            }
        }
        return;
    }

    // Check if subject is in the stack
    int stack_index = -1;
    for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
        if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
            stack_index = i;
            break;
        }
    }
    if (stack_index < 0) return;

    // Check if subject is in the active formatting list
    int fmt_index = -1;
    for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
        if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
        if (active_formatting_elements_[static_cast<u32>(i)] &&
            active_formatting_elements_[static_cast<u32>(i)]->tag_name == subject) {
            fmt_index = i;
            break;
        }
    }
    if (fmt_index < 0) {
        // Not in the list — just close subject element
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
                generate_implied_end_tags({subject});
                stack_.resize(static_cast<u32>(i));
                return;
            }
        }
        return;
    }

    // Full adoption agency per WHATWG spec (simplified)
    for (int outer = 0; outer < 8; outer++) {
        // Find formatting element
        Element* formatting_element = nullptr;
        int formatting_index = -1;
        for (i32 i = static_cast<i32>(active_formatting_elements_.size()) - 1; i >= 0; i--) {
            if (active_formatting_elements_[static_cast<u32>(i)] == nullptr) break;
            if (active_formatting_elements_[static_cast<u32>(i)] &&
                active_formatting_elements_[static_cast<u32>(i)]->tag_name == subject) {
                formatting_element = active_formatting_elements_[static_cast<u32>(i)];
                formatting_index = i;
                break;
            }
        }
        if (!formatting_element) return;

        // Find where it is in stack
        int formatting_stack_index = -1;
        for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
            if (stack_[static_cast<u32>(i)] == formatting_element) {
                formatting_stack_index = i;
                break;
            }
        }
        if (formatting_stack_index < 0) {
            // Not on the stack — remove from formatting list
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + formatting_index);
            return;
        }

        // Find furthest block after formatting_element in stack
        int furthest_block = -1;
        static const std::unordered_set<std::string> special_tags = {
            "address", "blockquote", "center", "dir", "div", "dl", "fieldset",
            "figure", "figcaption", "footer", "header", "hgroup", "main", "nav",
            "ol", "p", "section", "ul", "pre", "listing", "form", "table",
            "hr", "li", "dd", "dt", "h1", "h2", "h3", "h4", "h5", "h6"
        };
        for (i32 i = formatting_stack_index + 1; i < static_cast<i32>(stack_.size()); i++) {
            if (stack_[static_cast<u32>(i)] &&
                special_tags.find(stack_[static_cast<u32>(i)]->tag_name) != special_tags.end()) {
                furthest_block = i;
                break;
            }
        }

        if (furthest_block < 0) {
            // No furthest block — pop up to and including formatting element
            generate_implied_end_tags();
            for (i32 i = static_cast<i32>(stack_.size()) - 1; i >= 0; i--) {
                if (stack_[static_cast<u32>(i)] && stack_[static_cast<u32>(i)]->tag_name == subject) {
                    stack_.resize(static_cast<u32>(i));
                    break;
                }
            }
            active_formatting_elements_.erase(
                active_formatting_elements_.begin() + formatting_index);
            return;
        }

        // Simple approach: remove the formatting element from the stack
        if (formatting_stack_index >= 0) {
            generate_implied_end_tags({subject});
            stack_.resize(static_cast<u32>(formatting_stack_index));
        }
        active_formatting_elements_.erase(
            active_formatting_elements_.begin() + formatting_index);
        return;
    }
}

} // namespace browser::html
