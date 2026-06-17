#include "parser.hpp"

namespace browser::js {

std::unique_ptr<Pattern> Parser::parse_pattern_or_ident() {
    if (current_.type == TokenType::IDENTIFIER) {
        auto ident = std::make_unique<IdentPattern>();
        ident->line = current_.line;
        ident->column = current_.column;
        ident->name = current_.text;
        advance();
        return std::make_unique<Pattern>(std::move(*ident));
    }
    return parse_pattern();
}

std::unique_ptr<Pattern> Parser::parse_pattern() {
    if (current_.type == TokenType::LBRACE) {
        advance();
        auto obj = std::make_unique<ObjPattern>();
        obj->line = current_.line;
        obj->column = current_.column;
        while (current_.type != TokenType::RBRACE && current_.type != TokenType::EOF_TOKEN) {
            if (current_.type == TokenType::DOT_DOT_DOT) {
                advance();
                auto rest = parse_pattern();
                if (rest) {
                    obj->rest = std::move(rest);
                }
                break;
            }
            if (current_.type == TokenType::IDENTIFIER) {
                ObjPattern::Property prop;
                prop.key = current_.text;
                advance();
                if (match(TokenType::COLON)) {
                    prop.value = parse_pattern();
                    // Handle default value: {key: pattern = default}
                    if (match(TokenType::EQUALS)) {
                        prop.default_value = parse_expression(0);
                    }
                } else {
                    auto ip = std::make_unique<IdentPattern>();
                    ip->name = prop.key;
                    prop.value = std::make_unique<Pattern>(std::move(*ip));
                    // Handle default value: {key = default}
                    if (match(TokenType::EQUALS)) {
                        prop.default_value = parse_expression(0);
                    }
                }
                obj->properties.push_back(std::move(prop));
            } else {
                error("expected property name in object pattern");
                break;
            }
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RBRACE);
        return std::make_unique<Pattern>(std::move(*obj));
    }

    if (current_.type == TokenType::LBRACKET) {
        advance();
        auto arr = std::make_unique<ArrPattern>();
        arr->line = current_.line;
        arr->column = current_.column;
        while (current_.type != TokenType::RBRACKET && current_.type != TokenType::EOF_TOKEN) {
            if (current_.type == TokenType::COMMA) {
                arr->elements.push_back(nullptr);
                advance();
                continue;
            }
            if (current_.type == TokenType::DOT_DOT_DOT) {
                advance();
                auto rest = parse_pattern();
                if (rest) {
                    arr->rest = std::move(rest);
                }
                break;
            }
            auto elem = parse_pattern();
            if (elem) {
                // Handle default value: [pattern = default]
                if (match(TokenType::EQUALS)) {
                    // TODO: store default value in pattern
                }
                arr->elements.push_back(std::move(elem));
            }
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RBRACKET);
        return std::make_unique<Pattern>(std::move(*arr));
    }

    if (current_.type == TokenType::IDENTIFIER) {
        auto ident = std::make_unique<IdentPattern>();
        ident->line = current_.line;
        ident->column = current_.column;
        ident->name = current_.text;
        advance();
        return std::make_unique<Pattern>(std::move(*ident));
    }

    error("expected pattern");
    return nullptr;
}

} // namespace browser::js
