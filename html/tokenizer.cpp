#include "tokenizer.hpp"

namespace browser::html {

Tokenizer::Tokenizer() {}

void Tokenizer::set_state(int s) { state_ = static_cast<State>(s); }

void Tokenizer::set_appropriate_end_tag(const std::string& name) {
    appropriate_end_tag_ = name;
}

void Tokenizer::feed(const std::string& html) {
    input_ += html;
    process_input();
}

void Tokenizer::end() {
    process_input();
    if (!output_.empty()) return;

    // Process EOF through state transitions until the state stabilizes
    // (e.g. CHARACTER_REFERENCE → return_state_, then return_state_ handles EOF)
    unsigned safety = 0;
    for (;;) {
        State saved = state_;
        process_next_eof();
        if (!output_.empty()) return;
        if (state_ == saved || ++safety > 100) break;
    }

    if (output_.empty()) emit_eof();
}

bool Tokenizer::has_next() {
    if (output_.empty()) process_input();
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

void Tokenizer::emit(const Token& token) { output_.push(token); }

void Tokenizer::create_tag(TokenType type) {
    current_tag_ = TagToken();
    current_tag_.type = type;
    current_tag_.self_closing = false;
    current_tag_.attributes.clear();
}

void Tokenizer::emit_current_tag() { emit(Token(current_tag_)); }

void Tokenizer::emit_char(char32_t c) { CharacterToken ct; ct.character = c; emit(Token(ct)); }

void Tokenizer::emit_eof() { emit(Token(EOFToken())); }

bool Tokenizer::is_eof() const { return pos_ >= input_.size(); }

char Tokenizer::current() const {
    if (is_eof()) return '\0';
    return input_[pos_];
}

char Tokenizer::peek(u32 ahead) const {
    if (pos_ + ahead >= input_.size()) return '\0';
    return input_[pos_ + ahead];
}

char Tokenizer::advance() {
    if (is_eof()) return '\0';
    char c = input_[pos_];
    pos_++;
    if (c == '\r') {
        if (!is_eof() && input_[pos_] == '\n') pos_++;
        return '\n';
    }
    return c;
}

bool Tokenizer::consume_if(char expected) {
    if (current() == expected) { advance(); return true; }
    return false;
}

bool Tokenizer::is_ascii_upper_alpha(char c) { return c >= 'A' && c <= 'Z'; }
bool Tokenizer::is_ascii_lower_alpha(char c) { return c >= 'a' && c <= 'z'; }
bool Tokenizer::is_ascii_alpha(char c) { return is_ascii_upper_alpha(c) || is_ascii_lower_alpha(c); }
bool Tokenizer::is_ascii_alnum(char c) { return is_ascii_alpha(c) || (c >= '0' && c <= '9'); }
char Tokenizer::to_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

bool Tokenizer::name_matches() const {
    return appropriate_end_tag_.empty() || temporary_buffer_ == appropriate_end_tag_;
}

static i32 ent_cmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    if (*a == '\0' && *b == '\0') return 0;
    if (*a == '\0') return -1;
    if (*b == '\0') return 1;
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

// Encode a Unicode codepoint as UTF-8 bytes
static std::string utf8_encode(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xEF); s += static_cast<char>(0xBF); s += static_cast<char>(0xBD); // U+FFFD
    }
    return s;
}

static const NamedEntity* lookup_entity(const char* name) {
    i32 lo = 0;
    i32 hi = static_cast<i32>(HTML_ENTITIES_COUNT) - 1;
    while (lo <= hi) {
        i32 mid = lo + (hi - lo) / 2;
        i32 cmp = ent_cmp(name, HTML_ENTITIES[mid].name);
        if (cmp == 0) return &HTML_ENTITIES[mid];
        if (cmp < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return nullptr;
}

void Tokenizer::consume_char_ref() {
    u32 start = pos_;
    bool in_attr = (return_state_ == State::ATTRIBUTE_VALUE_DQ ||
                    return_state_ == State::ATTRIBUTE_VALUE_SQ ||
                    return_state_ == State::ATTRIBUTE_VALUE_UQ);

    auto emit_or_buffer = [&](char32_t cp) {
        if (in_attr) {
            temporary_buffer_ += utf8_encode(cp);
        } else {
            emit_char(cp);
        }
    };

    if (is_eof()) { emit_or_buffer('&'); return; }

    if (current() == '#') {
        advance();
        char32_t cp = 0;
        bool hex = false;
        bool overflow = false;
        if (current() == 'x' || current() == 'X') { hex = true; advance(); }
        while (!is_eof()) {
            char c = current();
            u32 digit = 0xFFFFFFFF;
            if (c >= '0' && c <= '9') digit = static_cast<u32>(c - '0');
            else if (hex && c >= 'a' && c <= 'f') digit = static_cast<u32>(c - 'a' + 10);
            else if (hex && c >= 'A' && c <= 'F') digit = static_cast<u32>(c - 'A' + 10);
            else break;
            if (!overflow) {
                u32 base = hex ? 16 : 10;
                if (cp > (0x10FFFF / base)) {
                    overflow = true;
                } else {
                    cp = static_cast<char32_t>(cp * base + digit);
                    if (cp > 0x10FFFF) overflow = true;
                }
            }
            advance();
        }
        if (!consume_if(';')) { pos_ = start; emit_or_buffer('&'); return; }
        if (overflow || cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = kReplacementChar;
        }
        emit_or_buffer(cp);
        return;
    }

    u32 saved = pos_;
    std::string name_buf;
    while (!is_eof()) {
        char c = current();
        if (is_ascii_alnum(c)) { name_buf += c; advance(); }
        else break;
    }

    while (!name_buf.empty()) {
        std::string with_semi = name_buf + ";";
        const NamedEntity* ent = lookup_entity(with_semi.c_str());
        if (ent && consume_if(';')) { emit_or_buffer(ent->codepoint); return; }

        ent = lookup_entity(name_buf.c_str());
        if (ent) {
            char next = current();
            if (is_ascii_alnum(next) || next == '=') {
                name_buf.pop_back();
                pos_ = saved + static_cast<u32>(name_buf.size());
                continue;
            }
            emit_or_buffer(ent->codepoint);
            return;
        }

        name_buf.pop_back();
        pos_ = saved + static_cast<u32>(name_buf.size());
    }

    pos_ = saved;
    emit_or_buffer('&');
}

void Tokenizer::process_next() {
    if (is_eof()) return;
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
            if (c == '&') { return_state_ = State::DATA; state_ = State::CHARACTER_REFERENCE; }
            else if (c == '<') { state_ = State::TAG_OPEN; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::RCDATA:
            if (c == '&') { return_state_ = State::RCDATA; state_ = State::CHARACTER_REFERENCE; }
            else if (c == '<') { state_ = State::RCDATA_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::RAWTEXT:
            if (c == '<') { state_ = State::RAWTEXT_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::PLAINTEXT:
            if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::SCRIPT_DATA:
            if (c == '<') { state_ = State::SCRIPT_DATA_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::TAG_OPEN:
            if (c == '!') { state_ = State::MARKUP_DECLARATION_OPEN; }
            else if (c == '/') { state_ = State::END_TAG_OPEN; }
            else if (is_ascii_alpha(c)) { create_tag(TokenType::START_TAG); temporary_buffer_ = to_lower(c); state_ = State::TAG_NAME; }
            else if (c == '\0' || c == '?') { emit_char('<'); state_ = State::DATA; }
            else { emit_char('<'); state_ = State::DATA; }
            break;

        case State::END_TAG_OPEN:
            if (is_ascii_alpha(c)) { create_tag(TokenType::END_TAG); temporary_buffer_ = to_lower(c); state_ = State::TAG_NAME; }
            else if (c == '>') { state_ = State::DATA; }
            else if (c == '\0') { emit_char('<'); emit_char('/'); state_ = State::DATA; }
            else { create_tag(TokenType::COMMENT); temporary_buffer_ = c; state_ = State::COMMENT; }
            break;

        case State::TAG_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') { current_tag_.tag_name = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/') { current_tag_.tag_name = temporary_buffer_; state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>') { current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_upper_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += c; }
            break;

        case State::RCDATA_LT:
            if (c == '/') { temporary_buffer_.clear(); state_ = State::RCDATA_END_TAG_OPEN; }
            else { emit_char('<'); reconsume_ = true; state_ = State::RCDATA; }
            break;

        case State::RCDATA_END_TAG_OPEN:
            if (is_ascii_alpha(c)) { create_tag(TokenType::END_TAG); temporary_buffer_ = to_lower(c); state_ = State::RCDATA_END_TAG_NAME; }
            else { emit_char('<'); emit_char('/'); reconsume_ = true; state_ = State::RCDATA; }
            break;

        case State::RCDATA_END_TAG_NAME:
            if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/' && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>' && name_matches()) { current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { emit_char('<'); emit_char('/'); for (char ch : temporary_buffer_) { emit_char(static_cast<unsigned char>(ch)); } reconsume_ = true; state_ = State::RCDATA; }
            break;

        case State::RAWTEXT_LT:
            if (c == '/') { temporary_buffer_.clear(); state_ = State::RAWTEXT_END_TAG_OPEN; }
            else { emit_char('<'); reconsume_ = true; state_ = State::RAWTEXT; }
            break;

        case State::RAWTEXT_END_TAG_OPEN:
            if (is_ascii_alpha(c)) { create_tag(TokenType::END_TAG); temporary_buffer_ = to_lower(c); state_ = State::RAWTEXT_END_TAG_NAME; }
            else { emit_char('<'); emit_char('/'); reconsume_ = true; state_ = State::RAWTEXT; }
            break;

        case State::RAWTEXT_END_TAG_NAME:
            if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/' && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>' && name_matches()) { current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { emit_char('<'); emit_char('/'); for (char ch : temporary_buffer_) { emit_char(static_cast<unsigned char>(ch)); } reconsume_ = true; state_ = State::RAWTEXT; }
            break;

        case State::SCRIPT_DATA_LT:
            if (c == '/') { temporary_buffer_.clear(); state_ = State::SCRIPT_DATA_END_TAG_OPEN; }
            else if (c == '!') { state_ = State::SCRIPT_DATA_ESCAPE_START; emit_char('<'); emit_char('!'); }
            else { emit_char('<'); reconsume_ = true; state_ = State::SCRIPT_DATA; }
            break;

        case State::SCRIPT_DATA_ESCAPE_START:
            if (c == '-') { state_ = State::SCRIPT_DATA_ESCAPE_START_DASH; emit_char('-'); }
            else { reconsume_ = true; state_ = State::SCRIPT_DATA; }
            break;

        case State::SCRIPT_DATA_ESCAPE_START_DASH:
            if (c == '-') { state_ = State::SCRIPT_DATA_ESCAPED_DASH_DASH; emit_char('-'); }
            else { reconsume_ = true; state_ = State::SCRIPT_DATA; }
            break;

        case State::SCRIPT_DATA_END_TAG_OPEN:
            if (is_ascii_alpha(c)) { create_tag(TokenType::END_TAG); temporary_buffer_ = to_lower(c); state_ = State::SCRIPT_DATA_END_TAG_NAME; }
            else { emit_char('<'); emit_char('/'); reconsume_ = true; state_ = State::SCRIPT_DATA; }
            break;

        case State::SCRIPT_DATA_END_TAG_NAME:
            if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/' && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>' && name_matches()) { current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { emit_char('<'); emit_char('/'); for (char ch : temporary_buffer_) { emit_char(static_cast<unsigned char>(ch)); } reconsume_ = true; state_ = State::SCRIPT_DATA; }
            break;

        case State::SCRIPT_DATA_ESCAPED:
            if (c == '-') { state_ = State::SCRIPT_DATA_ESCAPED_DASH; emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_ESCAPED_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::SCRIPT_DATA_ESCAPED_DASH:
            if (c == '-') { state_ = State::SCRIPT_DATA_ESCAPED_DASH_DASH; emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_ESCAPED_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); state_ = State::SCRIPT_DATA_ESCAPED; } }
            else { emit_char(static_cast<unsigned char>(c)); state_ = State::SCRIPT_DATA_ESCAPED; }
            break;

        case State::SCRIPT_DATA_ESCAPED_DASH_DASH:
            if (c == '-') { emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_ESCAPED_LT; }
            else if (c == '>') { state_ = State::SCRIPT_DATA; emit_char('>'); }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); state_ = State::SCRIPT_DATA_ESCAPED; } }
            else { emit_char(static_cast<unsigned char>(c)); state_ = State::SCRIPT_DATA_ESCAPED; }
            break;

        case State::SCRIPT_DATA_ESCAPED_LT:
            if (c == '/') { temporary_buffer_.clear(); state_ = State::SCRIPT_DATA_ESCAPED_END_TAG_OPEN; }
            else if (c == '!') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; emit_char('<'); emit_char('!'); }
            else { emit_char('<'); reconsume_ = true; state_ = State::SCRIPT_DATA_ESCAPED; }
            break;

        case State::SCRIPT_DATA_ESCAPED_END_TAG_OPEN:
            if (is_ascii_alpha(c)) { create_tag(TokenType::END_TAG); temporary_buffer_ = to_lower(c); state_ = State::SCRIPT_DATA_ESCAPED_END_TAG_NAME; }
            else { emit_char('<'); emit_char('/'); reconsume_ = true; state_ = State::SCRIPT_DATA_ESCAPED; }
            break;

        case State::SCRIPT_DATA_ESCAPED_END_TAG_NAME:
            if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/' && name_matches()) { current_tag_.tag_name = temporary_buffer_; state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>' && name_matches()) { current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { emit_char('<'); emit_char('/'); for (char ch : temporary_buffer_) { emit_char(static_cast<unsigned char>(ch)); } reconsume_ = true; state_ = State::SCRIPT_DATA_ESCAPED; }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED:
            if (c == '-') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH; emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); } }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
            if (c == '-') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH; emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT; }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; } }
            else { emit_char(static_cast<unsigned char>(c)); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
            if (c == '-') { emit_char('-'); }
            else if (c == '<') { state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_LT; }
            else if (c == '>') { state_ = State::SCRIPT_DATA; emit_char('>'); }
            else if (c == '\0') { if (is_eof()) { emit_eof(); } else { emit_char(kReplacementChar); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; } }
            else { emit_char(static_cast<unsigned char>(c)); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED_LT:
            if (c == '/') { temporary_buffer_.clear(); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN; }
            else { emit_char('<'); reconsume_ = true; state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN:
            if (is_ascii_alpha(c)) { temporary_buffer_ = to_lower(c); state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME; }
            else { emit_char('<'); emit_char('/'); reconsume_ = true; state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; }
            break;

        case State::SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME:
            if ((c == ' ' || c == '\t' || c == '\n' || c == '\f') && temporary_buffer_ == "script") { state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '/' && temporary_buffer_ == "script") { state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>' && temporary_buffer_ == "script") { create_tag(TokenType::END_TAG); current_tag_.tag_name = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { emit_char('<'); emit_char('/'); for (char ch : temporary_buffer_) { emit_char(static_cast<unsigned char>(ch)); } reconsume_ = true; state_ = State::SCRIPT_DATA_DOUBLE_ESCAPED; }
            break;

        case State::BEFORE_ATTRIBUTE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '/') { state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '>') { emit_current_tag(); state_ = State::DATA; }
            else if (c == '\0') { Attribute attr; attr.name = static_cast<char>(0xFD); current_tag_.attributes.push_back(attr); temporary_buffer_.clear(); state_ = State::ATTRIBUTE_NAME; }
            else { std::string n; n += to_lower(c); Attribute attr; attr.name = n; current_tag_.attributes.push_back(attr); temporary_buffer_.clear(); state_ = State::ATTRIBUTE_NAME; }
            break;

        case State::ATTRIBUTE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') { state_ = State::AFTER_ATTRIBUTE_NAME; }
            else if (c == '/') { state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '=') { state_ = State::BEFORE_ATTRIBUTE_VALUE; }
            else if (c == '>') { emit_current_tag(); state_ = State::DATA; }
            else if (is_ascii_upper_alpha(c)) { current_tag_.attributes.back().name += to_lower(c); }
            else if (c == '\0') { current_tag_.attributes.back().name += static_cast<char>(0xFD); }
            else { current_tag_.attributes.back().name += c; }
            break;

        case State::AFTER_ATTRIBUTE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '/') { state_ = State::SELF_CLOSING_START_TAG; }
            else if (c == '=') { state_ = State::BEFORE_ATTRIBUTE_VALUE; }
            else if (c == '>') { emit_current_tag(); state_ = State::DATA; }
            else if (c == '\0') { Attribute attr; attr.name = static_cast<char>(0xFD); current_tag_.attributes.push_back(attr); temporary_buffer_.clear(); state_ = State::ATTRIBUTE_NAME; }
            else { std::string n; n += to_lower(c); Attribute attr; attr.name = n; current_tag_.attributes.push_back(attr); temporary_buffer_.clear(); state_ = State::ATTRIBUTE_NAME; }
            break;

        case State::BEFORE_ATTRIBUTE_VALUE:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '"') { temporary_buffer_.clear(); state_ = State::ATTRIBUTE_VALUE_DQ; }
            else if (c == '\'') { temporary_buffer_.clear(); state_ = State::ATTRIBUTE_VALUE_SQ; }
            else if (c == '>') { emit_current_tag(); state_ = State::DATA; }
            else if (c == '\0') { temporary_buffer_.clear(); temporary_buffer_ += static_cast<char>(0xFD); state_ = State::ATTRIBUTE_VALUE_UQ; }
            else { temporary_buffer_.clear(); state_ = State::ATTRIBUTE_VALUE_UQ; reconsume_ = true; }
            break;

        case State::ATTRIBUTE_VALUE_DQ:
            if (c == '"') { current_tag_.attributes.back().value = temporary_buffer_; state_ = State::AFTER_ATTRIBUTE_NAME; }
            else if (c == '&') { return_state_ = State::ATTRIBUTE_VALUE_DQ; state_ = State::CHARACTER_REFERENCE; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += c; }
            break;

        case State::ATTRIBUTE_VALUE_SQ:
            if (c == '\'') { current_tag_.attributes.back().value = temporary_buffer_; state_ = State::AFTER_ATTRIBUTE_NAME; }
            else if (c == '&') { return_state_ = State::ATTRIBUTE_VALUE_SQ; state_ = State::CHARACTER_REFERENCE; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += c; }
            break;

        case State::ATTRIBUTE_VALUE_UQ:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') { current_tag_.attributes.back().value = temporary_buffer_; state_ = State::BEFORE_ATTRIBUTE_NAME; }
            else if (c == '&') { return_state_ = State::ATTRIBUTE_VALUE_UQ; state_ = State::CHARACTER_REFERENCE; }
            else if (c == '>') { current_tag_.attributes.back().value = temporary_buffer_; emit_current_tag(); state_ = State::DATA; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += c; }
            break;

        case State::SELF_CLOSING_START_TAG:
            if (c == '>') { current_tag_.self_closing = true; emit_current_tag(); state_ = State::DATA; }
            else if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '/') {}
            else { state_ = State::BEFORE_ATTRIBUTE_NAME; }
            break;

        case State::MARKUP_DECLARATION_OPEN:
            if (c == '-') { temporary_buffer_.clear(); state_ = State::COMMENT_START; }
            else if (c == 'D' || c == 'd') { temporary_buffer_ = to_lower(c); state_ = State::DOCTYPE; }
            else if (c == '[' && peek(0) == 'C' && peek(1) == 'D' && peek(2) == 'A' &&
                     peek(3) == 'T' && peek(4) == 'A' && peek(5) == '[') {
                advance(); advance(); advance(); advance(); advance(); advance();
                state_ = State::CDATA_SECTION;
            }
            else if (c == '\0') { emit_char('<'); emit_char('!'); state_ = State::DATA; }
            else { CommentToken ct; emit(Token(ct)); state_ = State::BOGUS_COMMENT; }
            break;

        case State::BOGUS_COMMENT:
            if (c == '>') { state_ = State::DATA; }
            break;

        case State::COMMENT_START:
            if (c == '-') { state_ = State::COMMENT_START_DASH; }
            else if (c == '>') { CommentToken ct; ct.data = temporary_buffer_; emit(Token(ct)); state_ = State::DATA; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); state_ = State::COMMENT; }
            else { temporary_buffer_ += c; state_ = State::COMMENT; }
            break;

        case State::COMMENT_START_DASH:
            if (c == '-') { state_ = State::COMMENT_END; }
            else if (c == '>') { CommentToken ct; ct.data = std::string("-"); emit(Token(ct)); state_ = State::DATA; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); state_ = State::COMMENT; }
            else { temporary_buffer_ += '-'; state_ = State::COMMENT; reconsume_ = true; }
            break;

        case State::COMMENT:
            if (c == '-') { state_ = State::COMMENT_END_DASH; }
            else if (c == '<') { temporary_buffer_ += c; state_ = State::COMMENT_LT; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += c; }
            break;

        case State::COMMENT_LT:
            if (c == '!') { temporary_buffer_ += c; state_ = State::COMMENT_LT_BANG; }
            else if (c != '-') { temporary_buffer_ += c; state_ = State::COMMENT; }
            else { state_ = State::COMMENT; }
            break;

        case State::COMMENT_LT_BANG:
            if (c == '-') { state_ = State::COMMENT_LT_BANG_DASH; }
            else { state_ = State::COMMENT; }
            break;

        case State::COMMENT_LT_BANG_DASH:
            if (c == '-') { state_ = State::COMMENT_END; }
            else { state_ = State::COMMENT_END_DASH; }
            break;

        case State::COMMENT_END_DASH:
            if (c == '-') { state_ = State::COMMENT_END; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); }
            else { temporary_buffer_ += '-'; state_ = State::COMMENT; reconsume_ = true; }
            break;

        case State::COMMENT_END:
            if (c == '>') { CommentToken ct; ct.data = temporary_buffer_; emit(Token(ct)); state_ = State::DATA; }
            else if (c == '!') { state_ = State::COMMENT_END_BANG; }
            else if (c == '-') { temporary_buffer_ += '-'; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); state_ = State::COMMENT; }
            else { temporary_buffer_ += "--"; state_ = State::COMMENT; reconsume_ = true; }
            break;

        case State::COMMENT_END_BANG:
            if (c == '-') { temporary_buffer_ += "--!"; state_ = State::COMMENT_END_DASH; }
            else if (c == '>') { CommentToken ct; ct.data = temporary_buffer_; emit(Token(ct)); state_ = State::DATA; }
            else if (c == '\0') { temporary_buffer_ += static_cast<char>(0xFD); state_ = State::COMMENT; }
            else { temporary_buffer_ += "--!"; state_ = State::COMMENT; reconsume_ = true; }
            break;

        case State::DOCTYPE:
            if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                if (temporary_buffer_ == "doctype") { current_doctype_ = DoctypeToken(); state_ = State::BEFORE_DOCTYPE_NAME; }
                else { current_doctype_ = DoctypeToken(); current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            }
            else if (c == '>') {
                if (temporary_buffer_ == "doctype") { DoctypeToken dt; emit(Token(dt)); }
                else { current_doctype_ = DoctypeToken(); current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); }
                state_ = State::DATA;
            }
            else if (c == '\0') { state_ = State::DATA; }
            else { current_doctype_ = DoctypeToken(); current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::BEFORE_DOCTYPE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '>') { current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.name = static_cast<char>(0xFD); state_ = State::DOCTYPE_NAME; }
            else { std::string n; n += to_lower(c); current_doctype_.name = n; state_ = State::DOCTYPE_NAME; }
            break;

        case State::DOCTYPE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') { state_ = State::AFTER_DOCTYPE_NAME; }
            else if (c == '>') { DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (is_ascii_upper_alpha(c)) { current_doctype_.name += to_lower(c); }
            else if (c == '\0') { current_doctype_.name += static_cast<char>(0xFD); }
            else { current_doctype_.name += c; }
            break;

        case State::AFTER_DOCTYPE_NAME:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '>') { DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            else if (c == 'P' || c == 'p') { temporary_buffer_ = to_lower(c); state_ = State::AFTER_DOCTYPE_PUBLIC_KEYWORD; }
            else if (c == 'S' || c == 's') { temporary_buffer_ = to_lower(c); state_ = State::BEFORE_DOCTYPE_SYSTEM_KEYWORD; }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::AFTER_DOCTYPE_PUBLIC_KEYWORD:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                if (temporary_buffer_ == "public") { state_ = State::BEFORE_DOCTYPE_PUBLIC_ID; }
                else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            }
            else if (c == '"') { if (temporary_buffer_ == "public") { temporary_buffer_.clear(); state_ = State::DOCTYPE_PUBLIC_ID_DQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '\'') { if (temporary_buffer_ == "public") { temporary_buffer_.clear(); state_ = State::DOCTYPE_PUBLIC_ID_SQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '>') { current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::BEFORE_DOCTYPE_PUBLIC_ID:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else if (c == '"') { if (temporary_buffer_ == "public") { temporary_buffer_.clear(); state_ = State::DOCTYPE_PUBLIC_ID_DQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '\'') { if (temporary_buffer_ == "public") { temporary_buffer_.clear(); state_ = State::DOCTYPE_PUBLIC_ID_SQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '>') { current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::DOCTYPE_PUBLIC_ID_DQ:
            if (c == '"') { state_ = State::AFTER_DOCTYPE_PUBLIC_ID; }
            else if (c == '\0') { current_doctype_.public_id += static_cast<char>(0xFD); }
            else { current_doctype_.public_id += c; }
            break;

        case State::DOCTYPE_PUBLIC_ID_SQ:
            if (c == '\'') { state_ = State::AFTER_DOCTYPE_PUBLIC_ID; }
            else if (c == '\0') { current_doctype_.public_id += static_cast<char>(0xFD); }
            else { current_doctype_.public_id += c; }
            break;

        case State::AFTER_DOCTYPE_PUBLIC_ID:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') { state_ = State::BEFORE_DOCTYPE_SYSTEM_ID; }
            else if (c == '"') { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_DQ; }
            else if (c == '\'') { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_SQ; }
            else if (c == '>') { DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::BEFORE_DOCTYPE_SYSTEM_KEYWORD:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
                if (temporary_buffer_ == "system") { state_ = State::BEFORE_DOCTYPE_SYSTEM_ID; }
                else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            }
            else if (c == '"') { if (temporary_buffer_ == "system") { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_DQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '\'') { if (temporary_buffer_ == "system") { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_SQ; } else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; } }
            else if (c == '>') { current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::BEFORE_DOCTYPE_SYSTEM_ID:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (is_ascii_alpha(c)) { temporary_buffer_ += to_lower(c); }
            else if (c == '"') { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_DQ; }
            else if (c == '\'') { temporary_buffer_.clear(); state_ = State::DOCTYPE_SYSTEM_ID_SQ; }
            else if (c == '>') { current_doctype_.force_quirks = true; DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::DOCTYPE_SYSTEM_ID_DQ:
            if (c == '"') { state_ = State::AFTER_DOCTYPE_SYSTEM_ID; }
            else if (c == '\0') { current_doctype_.system_id += static_cast<char>(0xFD); }
            else { current_doctype_.system_id += c; }
            break;

        case State::DOCTYPE_SYSTEM_ID_SQ:
            if (c == '\'') { state_ = State::AFTER_DOCTYPE_SYSTEM_ID; }
            else if (c == '\0') { current_doctype_.system_id += static_cast<char>(0xFD); }
            else { current_doctype_.system_id += c; }
            break;

        case State::AFTER_DOCTYPE_SYSTEM_ID:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {}
            else if (c == '>') { DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            else { current_doctype_.force_quirks = true; state_ = State::BOGUS_DOCTYPE; }
            break;

        case State::BOGUS_DOCTYPE:
            if (c == '>') { DoctypeToken dt = current_doctype_; emit(Token(dt)); state_ = State::DATA; }
            else if (c == '\0') {}
            break;

        case State::CHARACTER_REFERENCE:
            if (pos_ > 0) pos_--;
            consume_char_ref();
            state_ = return_state_;
            break;

        case State::CDATA_SECTION:
            if (c == ']') { state_ = State::CDATA_SECTION_BRACKET; }
            else if (c == '\0' && is_eof()) { emit_eof(); break; }
            else { emit_char(static_cast<unsigned char>(c)); }
            break;

        case State::CDATA_SECTION_BRACKET:
            if (c == ']') { state_ = State::CDATA_SECTION_END; }
            else { emit_char(']'); reconsume_ = true; state_ = State::CDATA_SECTION; }
            break;

        case State::CDATA_SECTION_END:
            if (c == '>') { state_ = State::DATA; }
            else { emit_char(']'); emit_char(']'); reconsume_ = true; state_ = State::CDATA_SECTION; }
            break;

        default: break;
    }
}

} // namespace browser::html
