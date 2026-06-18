#include "parser.hpp"

#include <cctype>

namespace browser::css {

    CssParser::CssParser(const std::string &input) : tokenizer_(input) {
        advance();
    }

    void CssParser::advance() {
        current_ = tokenizer_.next();
    }

    bool CssParser::is_simple_selector_start() const {
        if (current_.type == CssTokenType::IDENT)
            return true;
        if (current_.type == CssTokenType::HASH)
            return true;
        if (current_.type == CssTokenType::DELIM && (current_.text == "." || current_.text == "*"))
            return true;
        if (current_.type == CssTokenType::OPEN_BRACKET)
            return true;
        if (current_.type == CssTokenType::COLON)
            return true;
        return false;
    }

    std::string CssParser::consume_function_body() {
        std::string result;
        u32 depth = 1;
        while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
            if (current_.type == CssTokenType::OPEN_PAREN)
                depth++;
            if (current_.type == CssTokenType::CLOSE_PAREN)
                depth--;
            if (depth > 0) {
                if (!result.empty() && current_.type != CssTokenType::CLOSE_PAREN)
                    result += ' ';
                if (current_.type != CssTokenType::OPEN_PAREN || depth > 1) {
                    if (current_.type == CssTokenType::DELIM)
                        result += current_.text;
                    else if (current_.type == CssTokenType::IDENT)
                        result += current_.text;
                    else if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION ||
                             current_.type == CssTokenType::PERCENTAGE) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.2f", current_.numeric_value);
                        std::string num = buf;
                        auto dot = num.find('.');
                        if (dot != std::string::npos) {
                            auto last = num.find_last_not_of('0');
                            if (last > dot)
                                num = num.substr(0, last + 1);
                            else if (last == dot)
                                num = num.substr(0, dot);
                        }
                        result += num;
                        if (current_.type == CssTokenType::DIMENSION)
                            result += current_.text;
                        else if (current_.type == CssTokenType::PERCENTAGE)
                            result += '%';
                    } else if (current_.type == CssTokenType::STRING)
                        result += '\"' + current_.text + '\"';
                    else if (current_.type == CssTokenType::URL)
                        result += "url(" + current_.text + ")";
                    else if (current_.type == CssTokenType::WHITESPACE)
                        result += ' ';
                    else if (current_.type == CssTokenType::COMMA)
                        result += ", ";
                    else if (current_.type == CssTokenType::COLON)
                        result += ':';
                    else if (current_.type == CssTokenType::SEMICOLON)
                        result += ';';
                    else if (current_.type == CssTokenType::AT_KEYWORD)
                        result += '@' + current_.text;
                    else if (current_.type == CssTokenType::FUNCTION)
                        result += current_.text + '(';
                    else if (current_.type == CssTokenType::HASH)
                        result += '#' + current_.text;
                }
                advance();
            }
        }
        if (current_.type == CssTokenType::CLOSE_PAREN) {
            result += ')';
            advance();
        }
        return result;
    }

    StyleSheet CssParser::parse() {
        StyleSheet sheet;
        while (true) {
            if (current_.type == CssTokenType::EOF_TOKEN)
                break;
            if (current_.type == CssTokenType::WHITESPACE) {
                advance();
                continue;
            }
            if (current_.type == CssTokenType::AT_KEYWORD) {
                sheet.at_rules.push_back(parse_at_rule());
            } else if (current_.type == CssTokenType::SEMICOLON) {
                advance();
            } else {
                sheet.rules.push_back(parse_rule());
            }
        }
        return sheet;
    }

    std::vector<Selector> CssParser::parse_selectors(const std::string &input) {
        CssParser p(input);
        std::vector<Selector> sels;
        while (p.current_.type != CssTokenType::EOF_TOKEN) {
            if (p.current_.type == CssTokenType::WHITESPACE || p.current_.type == CssTokenType::COMMA) {
                p.advance();
                continue;
            }
            sels.push_back(p.parse_selector());
            if (p.current_.type == CssTokenType::COMMA) {
                p.advance();
            }
        }
        return sels;
    }

    std::vector<Declaration> CssParser::parse_inline_declarations() {
        std::vector<Declaration> decls;
        while (current_.type != CssTokenType::EOF_TOKEN) {
            if (current_.type == CssTokenType::WHITESPACE || current_.type == CssTokenType::SEMICOLON) {
                advance();
                continue;
            }
            decls.push_back(parse_declaration());
            if (current_.type == CssTokenType::SEMICOLON) {
                advance();
            }
        }
        return decls;
    }

    Rule CssParser::parse_rule() {
        Rule rule;
        while (is_simple_selector_start()) {
            rule.selectors.push_back(parse_selector());
            if (current_.type == CssTokenType::WHITESPACE)
                advance();
            if (current_.type == CssTokenType::COMMA) {
                advance();
                if (current_.type == CssTokenType::WHITESPACE)
                    advance();
            } else {
                break;
            }
        }
        if (current_.type == CssTokenType::OPEN_BRACE) {
            advance();
        }
        while (current_.type != CssTokenType::CLOSE_BRACE && current_.type != CssTokenType::EOF_TOKEN) {
            if (current_.type == CssTokenType::WHITESPACE) {
                advance();
                continue;
            }
            if (current_.type == CssTokenType::SEMICOLON) {
                advance();
                continue;
            }
            rule.declarations.push_back(parse_declaration());
            if (current_.type == CssTokenType::SEMICOLON) {
                advance();
            }
        }
        if (current_.type == CssTokenType::CLOSE_BRACE) {
            advance();
        }
        return rule;
    }

}  // namespace browser::css
