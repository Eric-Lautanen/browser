#include "tokenizer.hpp"

namespace browser::html {

    Tokenizer::Tokenizer() {}

    void Tokenizer::set_state(int s) {
        state_ = static_cast<State>(s);
    }

    void Tokenizer::set_appropriate_end_tag(const std::string &name) {
        appropriate_end_tag_ = name;
    }

    void Tokenizer::feed(const std::string &html) {
        input_ += html;
        process_input();
    }

    void Tokenizer::end() {
        process_input();
        if (!output_.empty())
            return;

        unsigned safety = 0;
        for (;;) {
            State saved = state_;
            process_next_eof();
            if (!output_.empty())
                return;
            if (state_ == saved || ++safety > 100)
                break;
        }

        if (output_.empty())
            emit_eof();
    }

    bool Tokenizer::has_next() {
        if (output_.empty())
            process_input();
        return !output_.empty();
    }

    Token Tokenizer::next() {
        if (output_.empty()) {
            process_input();
        }
        if (output_.empty()) {
            emit_eof();
        }
        Token t = std::move(output_.front());
        output_.pop();
        return t;
    }

    void Tokenizer::emit(const Token &token) {
        output_.push(token);
    }

    void Tokenizer::create_tag(TokenType type) {
        current_tag_ = TagToken();
        current_tag_.type = type;
        current_tag_.self_closing = false;
        current_tag_.attributes.clear();
    }

    void Tokenizer::emit_current_tag() {
        emit(Token(current_tag_));
    }

    void Tokenizer::emit_char(char32_t c) {
        CharacterToken ct;
        ct.character = c;
        emit(Token(ct));
    }

    void Tokenizer::emit_eof() {
        emit(Token(EOFToken()));
    }

    bool Tokenizer::is_eof() const {
        return pos_ >= input_.size();
    }

    char Tokenizer::current() const {
        if (is_eof())
            return '\0';
        return input_[pos_];
    }

    char Tokenizer::peek(u32 ahead) const {
        if (pos_ + ahead >= input_.size())
            return '\0';
        return input_[pos_ + ahead];
    }

    char Tokenizer::advance() {
        if (is_eof())
            return '\0';
        char c = input_[pos_];
        pos_++;
        if (c == '\r') {
            if (!is_eof() && input_[pos_] == '\n')
                pos_++;
            return '\n';
        }
        return c;
    }

    bool Tokenizer::consume_if(char expected) {
        if (current() == expected) {
            advance();
            return true;
        }
        return false;
    }

    bool Tokenizer::is_ascii_upper_alpha(char c) {
        return c >= 'A' && c <= 'Z';
    }
    bool Tokenizer::is_ascii_lower_alpha(char c) {
        return c >= 'a' && c <= 'z';
    }
    bool Tokenizer::is_ascii_alpha(char c) {
        return is_ascii_upper_alpha(c) || is_ascii_lower_alpha(c);
    }
    bool Tokenizer::is_ascii_alnum(char c) {
        return is_ascii_alpha(c) || (c >= '0' && c <= '9');
    }
    char Tokenizer::to_lower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }

    bool Tokenizer::name_matches() const {
        return appropriate_end_tag_.empty() || temporary_buffer_ == appropriate_end_tag_;
    }

    void Tokenizer::process_next() {
        if (is_eof())
            return;
        if (reconsume_) {
            reconsume_ = false;
            process_char(input_[pos_ - 1]);
            return;
        }
        char c = advance();
        process_char(c);
    }

    void Tokenizer::process_next_eof() {
        process_char('\0');
    }

    void Tokenizer::process_input() {
        while (!is_eof() && output_.empty()) {
            process_next();
        }
    }

    void Tokenizer::process_char(char c) {
        switch (state_) {
            case State::DATA:
                process_data_state(c);
                break;
            case State::CHARACTER_REFERENCE:
                process_char_ref_state();
                break;
            case State::RCDATA:
            case State::RAWTEXT:
            case State::PLAINTEXT:
            case State::SCRIPT_DATA:
            case State::RCDATA_LT:
            case State::RCDATA_END_TAG_OPEN:
            case State::RCDATA_END_TAG_NAME:
            case State::RAWTEXT_LT:
            case State::RAWTEXT_END_TAG_OPEN:
            case State::RAWTEXT_END_TAG_NAME:
            case State::SCRIPT_DATA_LT:
            case State::SCRIPT_DATA_ESCAPE_START:
            case State::SCRIPT_DATA_ESCAPE_START_DASH:
            case State::SCRIPT_DATA_END_TAG_OPEN:
            case State::SCRIPT_DATA_END_TAG_NAME:
            case State::SCRIPT_DATA_ESCAPED:
            case State::SCRIPT_DATA_ESCAPED_DASH:
            case State::SCRIPT_DATA_ESCAPED_DASH_DASH:
            case State::SCRIPT_DATA_ESCAPED_LT:
            case State::SCRIPT_DATA_ESCAPED_END_TAG_OPEN:
            case State::SCRIPT_DATA_ESCAPED_END_TAG_NAME:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED_LT:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN:
            case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME:
                process_rcdata_state(c);
                break;
            case State::TAG_OPEN:
            case State::END_TAG_OPEN:
            case State::TAG_NAME:
            case State::BEFORE_ATTRIBUTE_NAME:
            case State::ATTRIBUTE_NAME:
            case State::AFTER_ATTRIBUTE_NAME:
            case State::BEFORE_ATTRIBUTE_VALUE:
            case State::ATTRIBUTE_VALUE_DQ:
            case State::ATTRIBUTE_VALUE_SQ:
            case State::ATTRIBUTE_VALUE_UQ:
            case State::SELF_CLOSING_START_TAG:
            case State::MARKUP_DECLARATION_OPEN:
            case State::BOGUS_COMMENT:
            case State::COMMENT_START:
            case State::COMMENT_START_DASH:
            case State::COMMENT:
            case State::COMMENT_LT:
            case State::COMMENT_LT_BANG:
            case State::COMMENT_LT_BANG_DASH:
            case State::COMMENT_END_DASH:
            case State::COMMENT_END:
            case State::COMMENT_END_BANG:
            case State::DOCTYPE:
            case State::BEFORE_DOCTYPE_NAME:
            case State::DOCTYPE_NAME:
            case State::AFTER_DOCTYPE_NAME:
            case State::AFTER_DOCTYPE_PUBLIC_KEYWORD:
            case State::BEFORE_DOCTYPE_PUBLIC_ID:
            case State::DOCTYPE_PUBLIC_ID_DQ:
            case State::DOCTYPE_PUBLIC_ID_SQ:
            case State::AFTER_DOCTYPE_PUBLIC_ID:
            case State::BEFORE_DOCTYPE_SYSTEM_KEYWORD:
            case State::BEFORE_DOCTYPE_SYSTEM_ID:
            case State::DOCTYPE_SYSTEM_ID_DQ:
            case State::DOCTYPE_SYSTEM_ID_SQ:
            case State::AFTER_DOCTYPE_SYSTEM_ID:
            case State::BOGUS_DOCTYPE:
                process_tag_state(c);
                break;
            case State::CDATA_SECTION:
            case State::CDATA_SECTION_BRACKET:
            case State::CDATA_SECTION_END:
                process_foreign_state(c);
                break;
            default:
                break;
        }
    }

}  // namespace browser::html
