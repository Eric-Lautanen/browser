#include "parser.hpp"

#include <cctype>

namespace browser::css {

    bool CssParser::parse_keyframe_block(KeyframeBlock &block) {
        // Parse keyframe selector: percentage or from/to
        while (current_.type != CssTokenType::OPEN_BRACE && current_.type != CssTokenType::EOF_TOKEN &&
               current_.type != CssTokenType::CLOSE_BRACE) {
            if (current_.type == CssTokenType::WHITESPACE) {
                advance();
                continue;
            }
            if (current_.type == CssTokenType::COMMA) {
                advance();
                continue;
            }

            if (current_.type == CssTokenType::PERCENTAGE) {
                block.positions.push_back(current_.numeric_value);
                advance();
            } else if (current_.type == CssTokenType::DIMENSION) {
                if (!current_.text.empty() && current_.text[0] == '%') {
                    block.positions.push_back(current_.numeric_value);
                } else {
                    block.positions.push_back(current_.numeric_value);
                }
                advance();
            } else if (current_.type == CssTokenType::IDENT) {
                std::string kw = current_.text;
                for (auto &c : kw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (kw == "from")
                    block.positions.push_back(0.0f);
                else if (kw == "to")
                    block.positions.push_back(100.0f);
                advance();
            } else {
                advance();
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
            block.declarations.push_back(parse_declaration());
            if (current_.type == CssTokenType::SEMICOLON)
                advance();
        }

        if (current_.type == CssTokenType::CLOSE_BRACE)
            advance();

        return !block.positions.empty();
    }

    KeyframesRule CssParser::parse_keyframes() {
        KeyframesRule kr;
        // Name comes from the prelude already consumed in parse_at_rule
        // Actually the name is the next token after @keyframes
        while (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::IDENT) {
            kr.name = current_.text;
            advance();
        }
        while (current_.type == CssTokenType::WHITESPACE) advance();

        if (current_.type == CssTokenType::OPEN_BRACE) {
            advance();
        }

        while (current_.type != CssTokenType::CLOSE_BRACE && current_.type != CssTokenType::EOF_TOKEN) {
            if (current_.type == CssTokenType::WHITESPACE) {
                advance();
                continue;
            }
            KeyframeBlock block;
            if (parse_keyframe_block(block)) {
                kr.blocks.push_back(std::move(block));
            }
        }

        if (current_.type == CssTokenType::CLOSE_BRACE)
            advance();

        return kr;
    }

    AtRule CssParser::parse_at_rule() {
        AtRule at;
        at.name = current_.text;
        advance();

        while (current_.type == CssTokenType::WHITESPACE) advance();

        // Handle @keyframes specially
        bool is_keyframes = false;
        {
            std::string lower_name;
            for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            is_keyframes = (lower_name == "keyframes" || lower_name == "-webkit-keyframes");
        }

        if (is_keyframes) {
            at.keyframes = parse_keyframes();
            return at;
        }

        while (current_.type != CssTokenType::OPEN_BRACE && current_.type != CssTokenType::SEMICOLON &&
               current_.type != CssTokenType::EOF_TOKEN && current_.type != CssTokenType::CLOSE_BRACE) {
            if (current_.type != CssTokenType::WHITESPACE) {
                if (!at.prelude.empty())
                    at.prelude += ' ';
                at.prelude += current_.text;
            }
            advance();
        }

        if (current_.type == CssTokenType::OPEN_BRACE) {
            advance();

            std::string lower_name;
            for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (lower_name == "font-face") {
                // @font-face silently ignored — skip to closing brace
                u32 brace_depth = 1;
                while (brace_depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                    if (current_.type == CssTokenType::OPEN_BRACE) {
                        brace_depth++;
                    } else if (current_.type == CssTokenType::CLOSE_BRACE) {
                        brace_depth--;
                        if (brace_depth == 0) {
                            advance();
                            break;
                        }
                    }
                    advance();
                }
            } else {
                while (current_.type != CssTokenType::CLOSE_BRACE && current_.type != CssTokenType::EOF_TOKEN) {
                    if (current_.type == CssTokenType::WHITESPACE) {
                        advance();
                        continue;
                    }
                    if (current_.type == CssTokenType::SEMICOLON) {
                        advance();
                        continue;
                    }
                    if (current_.type == CssTokenType::AT_KEYWORD) {
                        at.at_rules.push_back(parse_at_rule());
                    } else {
                        at.rules.push_back(parse_rule());
                    }
                }
            }
            if (current_.type == CssTokenType::CLOSE_BRACE)
                advance();
        }

        if (current_.type == CssTokenType::SEMICOLON)
            advance();

        return at;
    }

}  // namespace browser::css
