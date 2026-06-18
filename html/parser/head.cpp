#include "../dom.hpp"
#include "../parser.hpp"

namespace browser::html {

    void Parser::handle_before_head(const Token &token) {
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r')
                return;
        }
        if (token.index() == 2) {
            flush_pending_text();
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            flush_pending_text();
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG && tag.tag_name == "html") {
                handle_in_body(token);
                return;
            }
            if (tag.type == TokenType::START_TAG && tag.tag_name == "head") {
                auto *head = create_element_for_token(tag);
                insert_element(head);
                stack_.push_back(head);
                head_element_pointer_ = head;
                mode_ = InsertionMode::IN_HEAD;
                return;
            }
        }
        TagToken implied;
        implied.type = TokenType::START_TAG;
        implied.tag_name = "head";
        auto *head = create_element_for_token(implied);
        insert_element(head);
        stack_.push_back(head);
        head_element_pointer_ = head;
        mode_ = InsertionMode::IN_HEAD;
        handle_token(token);
    }

    void Parser::handle_in_head(const Token &token) {
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                insert_character(c);
                return;
            }
        }
        if (token.index() == 2) {
            flush_pending_text();
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            flush_pending_text();
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG) {
                if (tag.tag_name == "html") {
                    handle_in_body(token);
                    return;
                }
                if (tag.tag_name == "meta" || tag.tag_name == "base" || tag.tag_name == "basefont" ||
                    tag.tag_name == "bgsound" || tag.tag_name == "link" || tag.tag_name == "command") {
                    auto *el = create_element_for_token(tag);
                    insert_element(el);
                    stack_.push_back(el);
                    stack_.pop_back();
                    return;
                }
                if (tag.tag_name == "title") {
                    flush_pending_text();
                    auto *el = create_element_for_token(tag);
                    insert_element(el);
                    stack_.push_back(el);
                    tokenizer_->set_appropriate_end_tag("title");
                    tokenizer_->set_state(1);  // RCDATA
                    original_mode_ = mode_;
                    mode_ = InsertionMode::TEXT;
                    return;
                }
                if (tag.tag_name == "style" || tag.tag_name == "noframes") {
                    flush_pending_text();
                    auto *el = create_element_for_token(tag);
                    insert_element(el);
                    stack_.push_back(el);
                    tokenizer_->set_appropriate_end_tag(tag.tag_name);
                    tokenizer_->set_state(2);  // RAWTEXT
                    original_mode_ = mode_;
                    mode_ = InsertionMode::TEXT;
                    return;
                }
                if (tag.tag_name == "noscript") {
                    flush_pending_text();
                    auto *el = create_element_for_token(tag);
                    insert_element(el);
                    stack_.push_back(el);
                    tokenizer_->set_appropriate_end_tag("noscript");
                    tokenizer_->set_state(2);  // RAWTEXT
                    original_mode_ = mode_;
                    mode_ = InsertionMode::TEXT;
                    return;
                }
                if (tag.tag_name == "script") {
                    flush_pending_text();
                    auto *el = create_element_for_token(tag);
                    insert_element(el);
                    stack_.push_back(el);
                    tokenizer_->set_appropriate_end_tag("script");
                    tokenizer_->set_state(4);  // SCRIPT_DATA
                    original_mode_ = mode_;
                    mode_ = InsertionMode::TEXT;
                    return;
                }
                if (tag.tag_name == "head") {
                    return;
                }
            }
            if (tag.type == TokenType::END_TAG) {
                if (tag.tag_name == "head") {
                    flush_pending_text();
                    if (!stack_.empty())
                        stack_.pop_back();
                    head_element_pointer_ = nullptr;
                    mode_ = InsertionMode::AFTER_HEAD;
                    return;
                }
                if (tag.tag_name == "body" || tag.tag_name == "html" || tag.tag_name == "br") {
                    flush_pending_text();
                    if (!stack_.empty())
                        stack_.pop_back();
                    head_element_pointer_ = nullptr;
                    mode_ = InsertionMode::AFTER_HEAD;
                    handle_token(token);
                    return;
                }
            }
        }
        if (token.index() == 1 && std::get<TagToken>(token).type == TokenType::END_TAG) {
            return;  // Ignore unknown end tags per spec
        }
        if (!stack_.empty())
            stack_.pop_back();
        head_element_pointer_ = nullptr;
        mode_ = InsertionMode::AFTER_HEAD;
        handle_token(token);
    }

    void Parser::handle_after_head(const Token &token) {
        if (token.index() == 3) {
            char32_t c = std::get<CharacterToken>(token).character;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                insert_character(c);
                return;
            }
        }
        if (token.index() == 2) {
            flush_pending_text();
            insert_comment(std::get<CommentToken>(token).data);
            return;
        }
        if (token.index() == 1) {
            flush_pending_text();
            auto &tag = std::get<TagToken>(token);
            if (tag.type == TokenType::START_TAG) {
                if (tag.tag_name == "html") {
                    handle_in_body(token);
                    return;
                }
                if (tag.tag_name == "body") {
                    auto *body = create_element_for_token(tag);
                    insert_element(body);
                    stack_.push_back(body);
                    frameset_ok_ = false;
                    mode_ = InsertionMode::IN_BODY;
                    return;
                }
                if (tag.tag_name == "frameset") {
                    flush_pending_text();
                    auto *fs = create_element_for_token(tag);
                    insert_element(fs);
                    stack_.push_back(fs);
                    mode_ = InsertionMode::AFTER_HEAD_FRAMESET;
                    return;
                }
                if (tag.tag_name == "base" || tag.tag_name == "basefont" || tag.tag_name == "bgsound" ||
                    tag.tag_name == "link" || tag.tag_name == "meta" || tag.tag_name == "noframes" ||
                    tag.tag_name == "script" || tag.tag_name == "style" || tag.tag_name == "title" ||
                    tag.tag_name == "noscript") {
                    flush_pending_text();
                    TagToken head_implied;
                    head_implied.type = TokenType::START_TAG;
                    head_implied.tag_name = "head";
                    auto *head_stub = create_element_for_token(head_implied);
                    insert_element(head_stub);
                    stack_.push_back(head_stub);
                    head_element_pointer_ = head_stub;
                    mode_ = InsertionMode::IN_HEAD;
                    handle_token(token);
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
        flush_pending_text();
        TagToken implied;
        implied.type = TokenType::START_TAG;
        implied.tag_name = "body";
        auto *body = create_element_for_token(implied);
        insert_element(body);
        stack_.push_back(body);
        mode_ = InsertionMode::IN_BODY;
        handle_token(token);
    }

}  // namespace browser::html
