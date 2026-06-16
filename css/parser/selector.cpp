#include "parser.hpp"

#include <cctype>

namespace browser::css {

    Selector CssParser::parse_selector() {
        Selector sel;
        CompoundSelector current;
        while (is_simple_selector_start()) {
            current.simples.push_back(parse_simple_selector());
        }
        sel.compounds.push_back(std::move(current));
        while (current_.type != CssTokenType::COMMA && current_.type != CssTokenType::OPEN_BRACE &&
               current_.type != CssTokenType::EOF_TOKEN && current_.type != CssTokenType::CLOSE_BRACE) {
            Combinator comb = Combinator::DESCENDANT;
            bool has_combinator = false;
            if (current_.type == CssTokenType::WHITESPACE) {
                advance();
                has_combinator = true;
                comb = Combinator::DESCENDANT;
            }
            if (current_.type == CssTokenType::DELIM) {
                if (current_.text == ">") {
                    comb = Combinator::CHILD;
                    advance();
                    has_combinator = true;
                } else if (current_.text == "+") {
                    comb = Combinator::ADJACENT_SIBLING;
                    advance();
                    has_combinator = true;
                } else if (current_.text == "~") {
                    comb = Combinator::GENERAL_SIBLING;
                    advance();
                    has_combinator = true;
                }
            }
            if (!has_combinator)
                break;
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (!is_simple_selector_start())
                break;
            sel.combinators.push_back(comb);
            current = CompoundSelector{};
            while (is_simple_selector_start()) {
                current.simples.push_back(parse_simple_selector());
            }
            sel.compounds.push_back(std::move(current));
        }
        return sel;
    }

    SimpleSelector CssParser::parse_simple_selector() {
        SimpleSelector s;
        s.type = SimpleSelector::Type::TAG;

        if (current_.type == CssTokenType::IDENT) {
            s.type = SimpleSelector::Type::TAG;
            s.name = current_.text;
            advance();
        } else if (current_.type == CssTokenType::DELIM && current_.text == "*") {
            s.type = SimpleSelector::Type::UNIVERSAL;
            advance();
        } else if (current_.type == CssTokenType::HASH) {
            s.type = SimpleSelector::Type::ID;
            s.name = current_.text;
            advance();
        } else if (current_.type == CssTokenType::DELIM && current_.text == ".") {
            s.type = SimpleSelector::Type::CLASS;
            advance();
            if (current_.type == CssTokenType::IDENT) {
                s.name = current_.text;
                advance();
            }
        } else if (current_.type == CssTokenType::COLON) {
            advance();
            bool is_pseudo_element = false;
            if (current_.type == CssTokenType::COLON) {
                is_pseudo_element = true;
                advance();
            }
            if (is_pseudo_element) {
                s.type = SimpleSelector::Type::PSEUDO_ELEMENT;
                if (current_.type == CssTokenType::IDENT) {
                    s.name = current_.text;
                    advance();
                }
            } else {
                s.type = SimpleSelector::Type::PSEUDO_CLASS;
                if (current_.type == CssTokenType::IDENT) {
                    s.name = current_.text;
                    advance();
                } else if (current_.type == CssTokenType::FUNCTION) {
                    s.name = current_.text;
                    advance();
                    u32 depth = 1;
                    while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                        if (current_.type == CssTokenType::OPEN_PAREN)
                            depth++;
                        if (current_.type == CssTokenType::CLOSE_PAREN)
                            depth--;
                        if (depth > 0)
                            advance();
                    }
                    if (current_.type == CssTokenType::CLOSE_PAREN)
                        advance();
                }
            }
        } else if (current_.type == CssTokenType::OPEN_BRACKET) {
            s.type = SimpleSelector::Type::ATTRIBUTE;
            advance();
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (current_.type == CssTokenType::IDENT) {
                s.name = current_.text;
                advance();
            }
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (current_.type == CssTokenType::DELIM) {
                if (current_.text == "=") {
                    s.match_operator = '=';
                    advance();
                } else if (current_.text == "~" || current_.text == "|" || current_.text == "^" ||
                           current_.text == "$" || current_.text == "*") {
                    s.match_operator = current_.text[0];
                    advance();
                    if (current_.type == CssTokenType::DELIM && current_.text == "=") {
                        advance();
                    }
                }
            }
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (current_.type == CssTokenType::IDENT || current_.type == CssTokenType::STRING) {
                s.value = current_.text;
                advance();
            }
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (current_.type == CssTokenType::CLOSE_BRACKET)
                advance();
        }
        return s;
    }

}  // namespace browser::css
