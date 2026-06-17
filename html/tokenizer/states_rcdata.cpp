#include "tokenizer.hpp"

namespace browser::html {

    void Tokenizer::process_rcdata_state(char32_t c) {
        switch (state_) {
            case State::RCDATA:
                if (c == '&') {
                    return_state_ = State::RCDATA;
                    state_ = State::CHARACTER_REFERENCE;
                } else if (c == '<') {
                    state_ = State::RCDATA_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::RAWTEXT:
                if (c == '<') {
                    state_ = State::RAWTEXT_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::PLAINTEXT:
                if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::SCRIPT_DATA:
                if (c == '<') {
                    state_ = State::SCRIPT_DATA_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::RCDATA_LT:
                if (c == '/') {
                    temporary_buffer_.clear();
                    state_ = State::RCDATA_END_TAG_OPEN;
                } else {
                    emit_char('<');
                    reconsume_ = true;
                    state_ = State::RCDATA;
                }
                break;

            case State::RCDATA_END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    create_tag(TokenType::END_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::RCDATA_END_TAG_NAME;
                } else {
                    emit_char('<');
                    emit_char('/');
                    reconsume_ = true;
                    state_ = State::RCDATA;
                }
                break;

            case State::RCDATA_END_TAG_NAME:
                if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    emit_char('<');
                    emit_char('/');
                    for (char ch : temporary_buffer_) {
                        emit_char(static_cast<unsigned char>(ch));
                    }
                    reconsume_ = true;
                    state_ = State::RCDATA;
                }
                break;

            case State::RAWTEXT_LT:
                if (c == '/') {
                    temporary_buffer_.clear();
                    state_ = State::RAWTEXT_END_TAG_OPEN;
                } else {
                    emit_char('<');
                    reconsume_ = true;
                    state_ = State::RAWTEXT;
                }
                break;

            case State::RAWTEXT_END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    create_tag(TokenType::END_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::RAWTEXT_END_TAG_NAME;
                } else {
                    emit_char('<');
                    emit_char('/');
                    reconsume_ = true;
                    state_ = State::RAWTEXT;
                }
                break;

            case State::RAWTEXT_END_TAG_NAME:
                if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    emit_char('<');
                    emit_char('/');
                    for (char ch : temporary_buffer_) {
                        emit_char(static_cast<unsigned char>(ch));
                    }
                    reconsume_ = true;
                    state_ = State::RAWTEXT;
                }
                break;

            case State::SCRIPT_DATA_LT:
                if (c == '/') {
                    temporary_buffer_.clear();
                    state_ = State::SCRIPT_DATA_END_TAG_OPEN;
                } else if (c == '!') {
                    state_ = State::SCRIPT_DATA_ESCAPE_START;
                    emit_char('<');
                    emit_char('!');
                } else {
                    emit_char('<');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA;
                }
                break;

            case State::SCRIPT_DATA_ESCAPE_START:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_ESCAPE_START_DASH;
                    emit_char('-');
                } else {
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA;
                }
                break;

            case State::SCRIPT_DATA_ESCAPE_START_DASH:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_ESCAPED_DASH_DASH;
                    emit_char('-');
                } else {
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA;
                }
                break;

            case State::SCRIPT_DATA_END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    create_tag(TokenType::END_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::SCRIPT_DATA_END_TAG_NAME;
                } else {
                    emit_char('<');
                    emit_char('/');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA;
                }
                break;

            case State::SCRIPT_DATA_END_TAG_NAME:
                if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    emit_char('<');
                    emit_char('/');
                    for (char ch : temporary_buffer_) {
                        emit_char(static_cast<unsigned char>(ch));
                    }
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA;
                }
                break;

            case State::SCRIPT_DATA_ESCAPED:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_ESCAPED_DASH;
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_ESCAPED_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::SCRIPT_DATA_ESCAPED_DASH:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_ESCAPED_DASH_DASH;
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_ESCAPED_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                        state_ = State::SCRIPT_DATA_ESCAPED;
                    }
                } else {
                    emit_char(c);
                    state_ = State::SCRIPT_DATA_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_ESCAPED_DASH_DASH:
                if (c == '-') {
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_ESCAPED_LT;
                } else if (c == '>') {
                    state_ = State::SCRIPT_DATA;
                    emit_char('>');
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                        state_ = State::SCRIPT_DATA_ESCAPED;
                    }
                } else {
                    emit_char(c);
                    state_ = State::SCRIPT_DATA_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_ESCAPED_LT:
                if (c == '/') {
                    temporary_buffer_.clear();
                    state_ = State::SCRIPT_DATA_ESCAPED_END_TAG_OPEN;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ = to_lower(c);
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                    emit_char('<');
                    emit_char(c);
                } else {
                    emit_char('<');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_ESCAPED_END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    create_tag(TokenType::END_TAG);
                    temporary_buffer_ = to_lower(c);
                    state_ = State::SCRIPT_DATA_ESCAPED_END_TAG_NAME;
                } else {
                    emit_char('<');
                    emit_char('/');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_ESCAPED_END_TAG_NAME:
                if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>' && name_matches()) {
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    emit_char('<');
                    emit_char('/');
                    for (char ch : temporary_buffer_) {
                        emit_char(static_cast<unsigned char>(ch));
                    }
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH;
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                    }
                } else {
                    emit_char(c);
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
                if (c == '-') {
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH;
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT;
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                        state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                    }
                } else {
                    emit_char(c);
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
                if (c == '-') {
                    emit_char('-');
                } else if (c == '<') {
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT;
                } else if (c == '>') {
                    state_ = State::SCRIPT_DATA;
                    emit_char('>');
                } else if (c == '\0') {
                    if (is_eof()) {
                        emit_eof();
                    } else {
                        emit_char(kReplacementChar);
                        state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                    }
                } else {
                    emit_char(c);
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED_LT:
                if (c == '/') {
                    temporary_buffer_.clear();
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN;
                } else {
                    emit_char('<');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN:
                if (is_ascii_alpha(c)) {
                    temporary_buffer_ = to_lower(c);
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME;
                } else {
                    emit_char('<');
                    emit_char('/');
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                break;

            case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME:
                if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && temporary_buffer_ == "script") {
                    state_ = State::BEFORE_ATTRIBUTE_NAME;
                } else if (c == '/' && temporary_buffer_ == "script") {
                    state_ = State::SELF_CLOSING_START_TAG;
                } else if (c == '>' && temporary_buffer_ == "script") {
                    create_tag(TokenType::END_TAG);
                    current_tag_.tag_name = temporary_buffer_;
                    emit_current_tag();
                    state_ = State::DATA;
                } else if (is_ascii_alpha(c)) {
                    temporary_buffer_ += to_lower(c);
                } else {
                    emit_char('<');
                    emit_char('/');
                    for (char ch : temporary_buffer_) {
                        emit_char(static_cast<unsigned char>(ch));
                    }
                    reconsume_ = true;
                    state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED;
                }
                break;

            default:
                break;
        }
    }

}  // namespace browser::html
