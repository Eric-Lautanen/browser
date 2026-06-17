#include "tokenizer.hpp"

namespace browser::html {

    void Tokenizer::process_foreign_state(char32_t c) {
        switch (state_) {
            case State::CDATA_SECTION:
                if (c == ']') {
                    state_ = State::CDATA_SECTION_BRACKET;
                } else if (c == '\0' && is_eof()) {
                    emit_eof();
                    break;
                } else {
                    emit_char(c);
                }
                break;
            case State::CDATA_SECTION_BRACKET:
                if (c == ']') {
                    state_ = State::CDATA_SECTION_END;
                } else {
                    emit_char(']');
                    reconsume_ = true;
                    state_ = State::CDATA_SECTION;
                }
                break;
            case State::CDATA_SECTION_END:
                if (c == '>') {
                    state_ = State::DATA;
                } else {
                    emit_char(']');
                    emit_char(']');
                    reconsume_ = true;
                    state_ = State::CDATA_SECTION;
                }
                break;
            default:
                break;
        }
    }

}  // namespace browser::html
