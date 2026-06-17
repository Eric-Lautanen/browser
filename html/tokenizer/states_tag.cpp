#include "tokenizer.hpp"
#include "../utf8.hpp"

namespace browser::html {

    void Tokenizer::process_tag_state(char32_t c) {
        switch (state_) {
            case State::TAG_OPEN:
                if (c == '!') {
                    state_ = State::MARKUP_DECLARATION_OPEN;
                } else if (c == '/') {
                    state_ = State::END_TAG_OPEN;
                } else if (is_ascii_alpha(c)) {
                    create_tag(TokenType::START_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::TAG_NAME;
                } else if (c == '?' || (c == '\0' && !is_eof())) {
                    emit_char('<');
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    emit_eof();
                } else {
                    emit_char('<');
                    state_ = State::DATA;
                }
                break;

            case State::END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    create_tag(TokenType::END_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::TAG_NAME;
                } else if (c == '>') {
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    emit_char('<');
                    emit_char('/');
                    emit_eof();
                } else if (c == '\0') {
                    emit_char('<');
                    emit_char('/');
                    state_ = State::DATA;
                } else {
                    create_tag(TokenType::COMMENT);
                    temporary_buffer_ = c;
                    state_ = State::COMMENT;
                }
                break;

            case State::TAG_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/') {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>') {
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_upper_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else if (c == '\0' && is_eof()) {
                    // Emit the tag as-is and reconsume EOF in DATA
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += c;
                }
                break;

            case State::BEFORE_ATTRIBUTE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '/') {
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>') {
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    Attribute attr;
                    attr.name = static_cast<char>(0xFD);
                    current_tag_.attributes.push_back(attr);
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_NAME;
                } else {
                    std::string n;
                    n += to_lower(c);
                    Attribute attr;
                    attr.name = n;
                    current_tag_.attributes.push_back(attr);
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_NAME;
                }
                break;

            case State::ATTRIBUTE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    state_ = State::AFTER_ATTRIBUTE_NAME;
                } else if (c == '/') {
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '=') {
                    state_ = State::BEFORE_ATTRIBUTE_VALUE;
                } else if (c == '>') {
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_upper_alpha(c)) {
                    current_tag_.attributes.back().name += to_lower(c);
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    current_tag_.attributes.back().name += static_cast<char>(0xFD);
                } else {
                    current_tag_.attributes.back().name += c;
                }
                break;

            case State::AFTER_ATTRIBUTE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '/') {
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '=') {
                    state_ = State::BEFORE_ATTRIBUTE_VALUE;
                } else if (c == '>') {
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    Attribute attr;
                    attr.name = static_cast<char>(0xFD);
                    current_tag_.attributes.push_back(attr);
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_NAME;
                } else {
                    std::string n;
                    n += to_lower(c);
                    Attribute attr;
                    attr.name = n;
                    current_tag_.attributes.push_back(attr);
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_NAME;
                }
                break;

            case State::BEFORE_ATTRIBUTE_VALUE:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '"') {
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_VALUE_DQ;
                } else if (c == '\'') {
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_VALUE_SQ;
                } else if (c == '>') {
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    temporary_buffer_.clear();
                    temporary_buffer_ += static_cast<char>(0xFD);
                    state_ = State::ATTRIBUTE_VALUE_UQ;
                } else {
                    temporary_buffer_.clear();
                    state_ = State::ATTRIBUTE_VALUE_UQ;
                    reconsume_ = true;
                }
                break;

            case State::ATTRIBUTE_VALUE_DQ:
                if (c == '"') {
                    current_tag_.attributes.back().value = temporary_buffer_;
                    state_ = State::AFTER_ATTRIBUTE_NAME;
                } else if (c == '&') {
                    return_state_ = State::ATTRIBUTE_VALUE_DQ;
                    state_ = State::CHARACTER_REFERENCE;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += encode_utf8(c);
                }
                break;

            case State::ATTRIBUTE_VALUE_SQ:
                if (c == '\'') {
                    current_tag_.attributes.back().value = temporary_buffer_;
                    state_ = State::AFTER_ATTRIBUTE_NAME;
                } else if (c == '&') {
                    return_state_ = State::ATTRIBUTE_VALUE_SQ;
                    state_ = State::CHARACTER_REFERENCE;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += encode_utf8(c);
                }
                break;

            case State::ATTRIBUTE_VALUE_UQ:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    current_tag_.attributes.back().value = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '&') {
                    return_state_ = State::ATTRIBUTE_VALUE_UQ;
                    state_ = State::CHARACTER_REFERENCE;
                } else if (c == '>') {
                    current_tag_.attributes.back().value = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += encode_utf8(c);
                }
                break;

            case State::SELF_CLOSING_START_TAG:
                if (c == '>') {
                    current_tag_.self_closing = true;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '/') {
                } else if (c == '\0' && is_eof()) {
                    state_ = State::DATA;
                    reconsume_ = true;
                } else {
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                }
                break;

            case State::MARKUP_DECLARATION_OPEN:
                if (c == '-') {
                    temporary_buffer_.clear();
                    state_ = State::COMMENT_START;
                } else if (c == 'D' || c == 'd') {
                    temporary_buffer_ = to_lower(c);
                    state_ = State::DOCTYPE;
                } else if (c == '[' && peek(0) == 'C' && peek(1) == 'D' && peek(2) == 'A' && peek(3) == 'T' &&
                           peek(4) == 'A' && peek(5) == '[') {
                    advance();
                    advance();
                    advance();
                    advance();
                    advance();
                    advance();
                    state_ = State::CDATA_SECTION;
                } else if (c == '\0' && is_eof()) {
                    emit_char('<');
                    emit_char('!');
                    emit_eof();
                } else if (c == '\0') {
                    emit_char('<');
                    emit_char('!');
                    state_ = State::DATA;
                } else {
                    temporary_buffer_.clear();
                    temporary_buffer_ += c;
                    state_ = State::BOGUS_COMMENT;
                }
                break;

            case State::BOGUS_COMMENT:
                if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += c;
                }
                break;

            case State::COMMENT_START:
                if (c == '-') {
                    state_ = State::COMMENT_START_DASH;
                } else if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                    state_ = State::COMMENT;
                } else {
                    temporary_buffer_ += c;
                    state_ = State::COMMENT;
                }
                break;

            case State::COMMENT_START_DASH:
                if (c == '-') {
                    state_ = State::COMMENT_END;
                } else if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    CommentToken ct;
                    ct.data = temporary_buffer_ + "-";
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                    state_ = State::COMMENT;
                } else {
                    state_ = State::COMMENT;
                    reconsume_ = true;
                }
                break;

            case State::COMMENT:
                if (c == '-') {
                    state_ = State::COMMENT_END_DASH;
                } else if (c == '<') {
                    temporary_buffer_ += c;
                    state_ = State::COMMENT_LT;
                } else if (c == '\0' && is_eof()) {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += c;
                }
                break;

            case State::COMMENT_LT:
                if (c == '!') {
                    temporary_buffer_ += c;
                    state_ = State::COMMENT_LT_BANG;
                } else if (c != '-') {
                    temporary_buffer_ += c;
                    state_ = State::COMMENT;
                } else {
                    state_ = State::COMMENT;
                }
                break;

            case State::COMMENT_LT_BANG:
                if (c == '-') {
                    state_ = State::COMMENT_LT_BANG_DASH;
                } else {
                    state_ = State::COMMENT;
                }
                break;

            case State::COMMENT_LT_BANG_DASH:
                if (c == '-') {
                    state_ = State::COMMENT_END;
                } else {
                    state_ = State::COMMENT_END_DASH;
                }
                break;

            case State::COMMENT_END_DASH:
                if (c == '-') {
                    state_ = State::COMMENT_END;
                } else if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    temporary_buffer_ += '-';
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                } else {
                    temporary_buffer_ += '-';
                    state_ = State::COMMENT;
                    reconsume_ = true;
                }
                break;

            case State::COMMENT_END:
                if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '!') {
                    state_ = State::COMMENT_END_BANG;
                } else if (c == '-') {
                    state_ = State::COMMENT_END_DASH;
                } else if (c == '\0' && is_eof()) {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                    state_ = State::COMMENT;
                } else {
                    temporary_buffer_ += "--";
                    state_ = State::COMMENT;
                    reconsume_ = true;
                }
                break;

            case State::COMMENT_END_BANG:
                if (c == '-') {
                    temporary_buffer_ += "--!";
                    state_ = State::COMMENT_END_DASH;
                } else if (c == '>') {
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    temporary_buffer_ += "--!";
                    CommentToken ct;
                    ct.data = temporary_buffer_;
                    emit(Token(ct));
                    emit_eof();
                } else if (c == '\0') {
                    temporary_buffer_ += static_cast<char>(0xFD);
                    state_ = State::COMMENT;
                } else {
                    temporary_buffer_ += "--!";
                    state_ = State::COMMENT;
                    reconsume_ = true;
                }
                break;

            case State::DOCTYPE:
                if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    if (temporary_buffer_ == "doctype") {
                        current_doctype_ = DoctypeToken();
                        state_ = State::BEFORE_DOCTYPE_NAME;
                    } else {
                        current_doctype_ = DoctypeToken();
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '>') {
                    if (temporary_buffer_ == "doctype") {
                        DoctypeToken dt;
                        emit(Token(dt));
                    } else {
                        current_doctype_ = DoctypeToken();
                        current_doctype_.force_quirks = true;
                        DoctypeToken dt = current_doctype_;
                        emit(Token(dt));
                    }
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_ = DoctypeToken();
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_ = DoctypeToken();
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else {
                    current_doctype_ = DoctypeToken();
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::BEFORE_DOCTYPE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '>') {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.name = static_cast<char>(0xFD);
                    state_ = State::DOCTYPE_NAME;
                } else {
                    std::string n;
                    n += to_lower(c);
                    current_doctype_.name = n;
                    state_ = State::DOCTYPE_NAME;
                }
                break;

            case State::DOCTYPE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    state_ = State::AFTER_DOCTYPE_NAME;
                } else if (c == '>') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (is_ascii_upper_alpha(c)) {
                    current_doctype_.name += to_lower(c);
                } else if (c == '\0' && is_eof()) {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.name += static_cast<char>(0xFD);
                } else {
                    current_doctype_.name += c;
                }
                break;

            case State::AFTER_DOCTYPE_NAME:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '>') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else if (c == 'P' || c == 'p') {
                    temporary_buffer_ = to_lower(c);
                    state_ = State::AFTER_DOCTYPE_PUBLIC_KEYWORD;
                } else if (c == 'S' || c == 's') {
                    temporary_buffer_ = to_lower(c);
                    state_ = State::BEFORE_DOCTYPE_SYSTEM_KEYWORD;
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::AFTER_DOCTYPE_PUBLIC_KEYWORD:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    if (temporary_buffer_ == "public") {
                        state_ = State::BEFORE_DOCTYPE_PUBLIC_ID;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '"') {
                    if (temporary_buffer_ == "public") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_PUBLIC_ID_DQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '\'') {
                    if (temporary_buffer_ == "public") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_PUBLIC_ID_SQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '>') {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::BEFORE_DOCTYPE_PUBLIC_ID:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '"') {
                    if (temporary_buffer_ == "public") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_PUBLIC_ID_DQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '\'') {
                    if (temporary_buffer_ == "public") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_PUBLIC_ID_SQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '>') {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0') {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::DOCTYPE_PUBLIC_ID_DQ:
                if (c == '"') {
                    state_ = State::AFTER_DOCTYPE_PUBLIC_ID;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.public_id += static_cast<char>(0xFD);
                } else {
                    current_doctype_.public_id += c;
                }
                break;

            case State::DOCTYPE_PUBLIC_ID_SQ:
                if (c == '\'') {
                    state_ = State::AFTER_DOCTYPE_PUBLIC_ID;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.public_id += static_cast<char>(0xFD);
                } else {
                    current_doctype_.public_id += c;
                }
                break;

            case State::AFTER_DOCTYPE_PUBLIC_ID:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    state_ = State::BEFORE_DOCTYPE_SYSTEM_ID;
                } else if (c == '"') {
                    temporary_buffer_.clear();
                    state_ = State::DOCTYPE_SYSTEM_ID_DQ;
                } else if (c == '\'') {
                    temporary_buffer_.clear();
                    state_ = State::DOCTYPE_SYSTEM_ID_SQ;
                } else if (c == '>') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::BEFORE_DOCTYPE_SYSTEM_KEYWORD:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                    if (temporary_buffer_ == "system") {
                        state_ = State::BEFORE_DOCTYPE_SYSTEM_ID;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '"') {
                    if (temporary_buffer_ == "system") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_SYSTEM_ID_DQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '\'') {
                    if (temporary_buffer_ == "system") {
                        temporary_buffer_.clear();
                        state_ = State::DOCTYPE_SYSTEM_ID_SQ;
                    } else {
                        current_doctype_.force_quirks = true;
                        state_ = State::BOGUS_DOCTYPE;
                    }
                } else if (c == '>') {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::BEFORE_DOCTYPE_SYSTEM_ID:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else if (c == '"') {
                    temporary_buffer_.clear();
                    state_ = State::DOCTYPE_SYSTEM_ID_DQ;
                } else if (c == '\'') {
                    temporary_buffer_.clear();
                    state_ = State::DOCTYPE_SYSTEM_ID_SQ;
                } else if (c == '>') {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::DOCTYPE_SYSTEM_ID_DQ:
                if (c == '"') {
                    state_ = State::AFTER_DOCTYPE_SYSTEM_ID;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.system_id += static_cast<char>(0xFD);
                } else {
                    current_doctype_.system_id += c;
                }
                break;

            case State::DOCTYPE_SYSTEM_ID_SQ:
                if (c == '\'') {
                    state_ = State::AFTER_DOCTYPE_SYSTEM_ID;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.system_id += static_cast<char>(0xFD);
                } else {
                    current_doctype_.system_id += c;
                }
                break;

            case State::AFTER_DOCTYPE_SYSTEM_ID:
                if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                } else if (c == '>') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    current_doctype_.force_quirks = true;
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                } else {
                    current_doctype_.force_quirks = true;
                    state_ = State::BOGUS_DOCTYPE;
                }
                break;

            case State::BOGUS_DOCTYPE:
                if (c == '>') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    state_ = State::DATA;
                } else if (c == '\0' && is_eof()) {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                    emit_eof();
                } else if (c == '\0') {
                    DoctypeToken dt = current_doctype_;
                    emit(Token(dt));
                }
                break;

            default:
                break;
        }
    }

}  // namespace browser::html
