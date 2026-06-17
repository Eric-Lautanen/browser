#include "parser.hpp"

#include <cctype>
#include <cstdlib>

namespace browser::css {

    static SimpleSelector::NthArgs parse_nth_args(const std::string &raw) {
        SimpleSelector::NthArgs nth;
        std::string inner = raw;
        while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t' || inner.back() == ')'))
            inner.pop_back();
        while (!inner.empty() && (inner[0] == ' ' || inner[0] == '\t')) inner = inner.substr(1);

        if (inner == "odd") { nth.is_odd = true; return nth; }
        if (inner == "even") { nth.is_even = true; return nth; }

        int n_pos = static_cast<int>(inner.find('n'));
        if (n_pos != -1) {
            std::string a_part = inner.substr(0, n_pos);
            if (a_part.empty() || a_part == "+") nth.a = 1;
            else if (a_part == "-") nth.a = -1;
            else nth.a = static_cast<i32>(std::strtol(a_part.c_str(), nullptr, 10));
            std::string b_part = inner.substr(n_pos + 1);
            while (!b_part.empty() && b_part[0] == ' ') b_part = b_part.substr(1);
            if (!b_part.empty()) {
                if (b_part[0] == '+') b_part = b_part.substr(1);
                nth.b = static_cast<i32>(std::strtol(b_part.c_str(), nullptr, 10));
            }
        } else {
            nth.b = static_cast<i32>(std::strtol(inner.c_str(), nullptr, 10));
        }
        return nth;
    }

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
                    std::string func_name = current_.text;
                    for (auto &c : func_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    s.name = func_name;
                    advance();
                    std::string inner;
                    u32 depth = 1;
                    while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                        if (current_.type == CssTokenType::OPEN_PAREN)
                            depth++;
                        if (current_.type == CssTokenType::CLOSE_PAREN)
                            depth--;
                        if (depth > 0) {
                            if (current_.type == CssTokenType::WHITESPACE) {
                                if (!inner.empty() && inner.back() != ' ') inner += ' ';
                            } else if (current_.type == CssTokenType::DELIM) {
                                inner += current_.text;
                            } else if (current_.type == CssTokenType::IDENT ||
                                       current_.type == CssTokenType::HASH) {
                                inner += current_.text;
                            } else if (current_.type == CssTokenType::NUMBER ||
                                       current_.type == CssTokenType::DIMENSION ||
                                       current_.type == CssTokenType::PERCENTAGE) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "%.0f", current_.numeric_value);
                                inner += buf;
                                if (current_.type == CssTokenType::DIMENSION)
                                    inner += current_.text;
                                else if (current_.type == CssTokenType::PERCENTAGE)
                                    inner += '%';
                            } else if (current_.type == CssTokenType::COLON) {
                                inner += ':';
                            } else if (current_.type == CssTokenType::COMMA) {
                                inner += ',';
                            } else if (current_.type == CssTokenType::OPEN_BRACKET) {
                                inner += '[';
                            } else if (current_.type == CssTokenType::CLOSE_BRACKET) {
                                inner += ']';
                            } else if (current_.type == CssTokenType::STRING) {
                                inner += '"' + current_.text + '"';
                            }
                            advance();
                        }
                    }
                    if (current_.type == CssTokenType::CLOSE_PAREN)
                        advance();

                    if (func_name == "not" || func_name == "is" || func_name == "where") {
                        s.argument_selectors = CssParser::parse_selectors(inner);
                    } else if (func_name == "nth-child" || func_name == "nth-last-child") {
                        s.nth_args = parse_nth_args(inner);
                    }
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
