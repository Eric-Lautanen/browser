#pragma once
#include "../entities.hpp"
#include "../token.hpp"

#include <queue>
#include <string>

namespace browser::html {

    class Tokenizer {
    public:
        Tokenizer();
        void feed(const std::string &html);
        void end();
        bool has_next();
        Token next();
        void set_appropriate_end_tag(const std::string &name);
        void set_state(int s);

    private:
        enum class State {
            DATA,
            RCDATA,
            RAWTEXT,
            PLAINTEXT,
            SCRIPT_DATA,
            TAG_OPEN,
            END_TAG_OPEN,
            TAG_NAME,
            RCDATA_LT,
            RCDATA_END_TAG_OPEN,
            RCDATA_END_TAG_NAME,
            RAWTEXT_LT,
            RAWTEXT_END_TAG_OPEN,
            RAWTEXT_END_TAG_NAME,
            SCRIPT_DATA_LT,
            SCRIPT_DATA_END_TAG_OPEN,
            SCRIPT_DATA_END_TAG_NAME,
            SCRIPT_DATA_ESCAPE_START,
            SCRIPT_DATA_ESCAPE_START_DASH,
            SCRIPT_DATA_ESCAPED,
            SCRIPT_DATA_ESCAPED_DASH,
            SCRIPT_DATA_ESCAPED_DASH_DASH,
            SCRIPT_DATA_ESCAPED_LT,
            SCRIPT_DATA_ESCAPED_END_TAG_OPEN,
            SCRIPT_DATA_ESCAPED_END_TAG_NAME,
            SCRIPT_DATA_DOUBLE_ESCAPED,
            SCRIPT_DATA_DOUBLE_ESCAPED_DASH,
            SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH,
            SCRIPT_DATA_DOUBLE_ESCAPED_LT,
            SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN,
            SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME,
            BEFORE_ATTRIBUTE_NAME,
            ATTRIBUTE_NAME,
            AFTER_ATTRIBUTE_NAME,
            BEFORE_ATTRIBUTE_VALUE,
            ATTRIBUTE_VALUE_DQ,
            ATTRIBUTE_VALUE_SQ,
            ATTRIBUTE_VALUE_UQ,
            SELF_CLOSING_START_TAG,
            MARKUP_DECLARATION_OPEN,
            COMMENT_START,
            COMMENT_START_DASH,
            COMMENT,
            COMMENT_LT,
            COMMENT_LT_BANG,
            COMMENT_LT_BANG_DASH,
            COMMENT_END_DASH,
            COMMENT_END,
            COMMENT_END_BANG,
            DOCTYPE,
            BEFORE_DOCTYPE_NAME,
            DOCTYPE_NAME,
            AFTER_DOCTYPE_NAME,
            AFTER_DOCTYPE_PUBLIC_KEYWORD,
            BEFORE_DOCTYPE_PUBLIC_ID,
            DOCTYPE_PUBLIC_ID_DQ,
            DOCTYPE_PUBLIC_ID_SQ,
            AFTER_DOCTYPE_PUBLIC_ID,
            BEFORE_DOCTYPE_SYSTEM_KEYWORD,
            BEFORE_DOCTYPE_SYSTEM_ID,
            DOCTYPE_SYSTEM_ID_DQ,
            DOCTYPE_SYSTEM_ID_SQ,
            AFTER_DOCTYPE_SYSTEM_ID,
            BOGUS_DOCTYPE,
            BOGUS_COMMENT,
            CHARACTER_REFERENCE,
            CDATA_SECTION,
            CDATA_SECTION_BRACKET,
            CDATA_SECTION_END
        };

        State state_ = State::DATA;
        State return_state_ = State::DATA;
        std::queue<Token> output_;
        std::string input_;
        u32 pos_ = 0;
        TagToken current_tag_;
        DoctypeToken current_doctype_;
        std::string temporary_buffer_;
        std::string appropriate_end_tag_;
        bool reconsume_ = false;

        void process_data_state(char32_t c);
        void process_rcdata_state(char32_t c);
        void process_tag_state(char32_t c);
        void process_foreign_state(char32_t c);
        void process_char_ref_state();

        void process_char(char32_t c);
        void emit(const Token &token);
        void create_tag(TokenType type);
        void emit_current_tag();
        void emit_char(char32_t c);
        void emit_eof();
        void process_input();
        void process_next();
        void process_next_eof();
        void consume_char_ref();
        char current() const;
        char peek(u32 ahead = 0) const;
        char advance();
        bool is_eof() const;

        bool consume_if(char expected);

        bool name_matches() const;

        static bool is_ascii_upper_alpha(char c);
        static bool is_ascii_lower_alpha(char c);
        static bool is_ascii_alpha(char c);
        static bool is_ascii_alnum(char c);
        static char to_lower(char c);

        static const char32_t kReplacementChar = 0xFFFD;
    };

}  // namespace browser::html
