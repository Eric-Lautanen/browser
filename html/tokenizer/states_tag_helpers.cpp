#include "states_tag_helpers.hpp"
#include "../utf8.hpp"

namespace browser::html {

void handle_tag_open(Tokenizer& t, char32_t c) {
    if (c == '!') t.state_ = Tokenizer::State::MARKUP_DECLARATION_OPEN;
    else if (c == '/') t.state_ = Tokenizer::State::END_TAG_OPEN;
    else if (Tokenizer::is_ascii_alpha(c)) { t.create_tag(TokenType::START_TAG); t.temporary_buffer_ = Tokenizer::to_lower(c); t.state_ = Tokenizer::State::TAG_NAME; }
    else if (c == '?' || (c == '\0' && !t.is_eof())) { t.emit_char('<'); t.state_ = Tokenizer::State::DATA; }
    else if (c == '\0' && t.is_eof()) t.emit_eof();
    else { t.emit_char('<'); t.state_ = Tokenizer::State::DATA; }
}
void handle_end_tag_open(Tokenizer& t, char32_t c) {
    if (Tokenizer::is_ascii_alpha(c)) { t.create_tag(TokenType::END_TAG); t.temporary_buffer_ = Tokenizer::to_lower(c); t.state_ = Tokenizer::State::TAG_NAME; }
    else if (c == '>') t.state_ = Tokenizer::State::DATA;
    else if (c == '\0' && t.is_eof()) { t.emit_char('<'); t.emit_char('/'); t.emit_eof(); }
    else if (c == '\0') { t.emit_char('<'); t.emit_char('/'); t.state_ = Tokenizer::State::DATA; }
    else { t.create_tag(TokenType::COMMENT); t.temporary_buffer_ = c; t.state_ = Tokenizer::State::COMMENT; }
}
void handle_tag_name(Tokenizer& t, char32_t c) { (void)t; (void)c; }

} // namespace browser::html
