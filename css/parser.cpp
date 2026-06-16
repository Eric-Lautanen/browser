#include "parser.hpp"
#include <cctype>
#include <optional>
#include <algorithm>

namespace browser::css {

CssParser::CssParser(const std::string& input) : tokenizer_(input) {
    advance();
}

void CssParser::advance() {
    current_ = tokenizer_.next();
}

// Error-recovery consume: advances past unexpected tokens instead of throwing
// (no-exceptions build; a real assertion would need a different approach)
void CssParser::expect(CssTokenType type) {
    if (current_.type != type) {
        advance();
    }
}

bool CssParser::is_simple_selector_start() const {
    if (current_.type == CssTokenType::IDENT) return true;
    if (current_.type == CssTokenType::HASH) return true;
    if (current_.type == CssTokenType::DELIM &&
        (current_.text == "." || current_.text == "*")) return true;
    if (current_.type == CssTokenType::OPEN_BRACKET) return true;
    if (current_.type == CssTokenType::COLON) return true;
    return false;
}

StyleSheet CssParser::parse() {
    StyleSheet sheet;
    while (true) {
        if (current_.type == CssTokenType::EOF_TOKEN) break;
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

Rule CssParser::parse_rule() {
    Rule rule;

    while (is_simple_selector_start()) {
        rule.selectors.push_back(parse_selector());
        if (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::COMMA) {
            advance();
            if (current_.type == CssTokenType::WHITESPACE) advance();
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

Selector CssParser::parse_selector() {
    Selector sel;
    CompoundSelector current;

    while (is_simple_selector_start()) {
        current.simples.push_back(parse_simple_selector());
    }
    sel.compounds.push_back(std::move(current));

    while (current_.type != CssTokenType::COMMA &&
           current_.type != CssTokenType::OPEN_BRACE &&
           current_.type != CssTokenType::EOF_TOKEN &&
           current_.type != CssTokenType::CLOSE_BRACE) {

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

        if (!has_combinator) break;

        if (current_.type == CssTokenType::WHITESPACE) advance();

        if (!is_simple_selector_start()) break;
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
        s.type = SimpleSelector::Type::PSEUDO_CLASS;
        if (current_.type == CssTokenType::IDENT) {
            s.name = current_.text;
            advance();
        } else if (current_.type == CssTokenType::FUNCTION) {
            s.name = current_.text;
            advance();
            u32 depth = 1;
            while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::OPEN_PAREN) depth++;
                if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
                if (depth > 0) advance();
            }
            if (current_.type == CssTokenType::CLOSE_PAREN) advance();
        }
    } else if (current_.type == CssTokenType::OPEN_BRACKET) {
        s.type = SimpleSelector::Type::ATTRIBUTE;
        advance();
        if (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::IDENT) {
            s.name = current_.text;
            advance();
        }
        if (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::DELIM) {
            if (current_.text == "=") {
                s.match_operator = '=';
                advance();
            } else if (current_.text == "~" || current_.text == "|" ||
                       current_.text == "^" || current_.text == "$" ||
                       current_.text == "*") {
                s.match_operator = current_.text[0];
                advance();
                if (current_.type == CssTokenType::DELIM && current_.text == "=") {
                    advance();
                }
            }
        }
        if (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::IDENT || current_.type == CssTokenType::STRING) {
            s.value = current_.text;
            advance();
        }
        if (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::CLOSE_BRACKET) advance();
    }

    return s;
}

Declaration CssParser::parse_declaration() {
    Declaration decl;

    if (current_.type == CssTokenType::IDENT) {
        decl.property = current_.text;
        advance();
    }

    if (current_.type == CssTokenType::WHITESPACE) advance();

    if (current_.type == CssTokenType::COLON) {
        advance();
    }

    if (current_.type == CssTokenType::WHITESPACE) advance();

    while (current_.type != CssTokenType::SEMICOLON &&
           current_.type != CssTokenType::CLOSE_BRACE &&
           current_.type != CssTokenType::EOF_TOKEN) {

        if (current_.type == CssTokenType::WHITESPACE) {
            advance();
            continue;
        }

        if (current_.type == CssTokenType::DELIM && current_.text == "!") {
            advance();
            if (current_.type == CssTokenType::WHITESPACE) advance();
            if (current_.type == CssTokenType::IDENT && current_.text == "important") {
                decl.important = true;
                advance();
            }
            continue;
        }

        decl.values.push_back(parse_value());
    }

    return decl;
}

CSSValue CssParser::parse_value() {
    CSSValue val;
    val.type = CSSValue::Type::KEYWORD;

    if (current_.type == CssTokenType::IDENT) {
        val.type = CSSValue::Type::KEYWORD;
        val.keyword = current_.text;
        advance();
    } else if (current_.type == CssTokenType::NUMBER) {
        val.type = CSSValue::Type::NUMBER;
        val.number = current_.numeric_value;
        advance();
    } else if (current_.type == CssTokenType::DIMENSION) {
        val.type = CSSValue::Type::LENGTH;
        val.length.value = current_.numeric_value;
        std::string lower;
        for (char c : current_.text) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower == "px") val.length.unit = Length::Unit::PX;
        else if (lower == "em") val.length.unit = Length::Unit::EM;
        else if (lower == "rem") val.length.unit = Length::Unit::REM;
        else if (lower == "%") val.length.unit = Length::Unit::PERCENT;
        else if (lower == "vw") val.length.unit = Length::Unit::VW;
        else if (lower == "vh") val.length.unit = Length::Unit::VH;
        else val.length.unit = Length::Unit::NONE;
        advance();
    } else if (current_.type == CssTokenType::PERCENTAGE) {
        val.type = CSSValue::Type::PERCENTAGE;
        val.number = current_.numeric_value;
        advance();
    } else if (current_.type == CssTokenType::STRING) {
        val.type = CSSValue::Type::STRING;
        val.string_value = current_.text;
        advance();
    } else if (current_.type == CssTokenType::URL) {
        val.type = CSSValue::Type::URL;
        val.string_value = current_.text;
        advance();
    } else if (current_.type == CssTokenType::FUNCTION) {
        std::string func_name = current_.text;
        for (auto& c : func_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        advance();

        // Try to parse rgb()/rgba()/hsl() color functions
        if (func_name == "rgb" || func_name == "rgba" || func_name == "hsl" || func_name == "hsla") {
            auto parse_color_args = [&]() -> std::optional<Color> {
                auto next_non_ws = [&]() -> const CssToken* {
                    const CssToken* t = &current_;
                    while (t->type == CssTokenType::WHITESPACE) { advance(); t = &current_; }
                    return t;
                };
                auto expect_num = [&]() -> std::optional<f32> {
                    auto* t = next_non_ws();
                    if (t->type == CssTokenType::NUMBER || t->type == CssTokenType::DIMENSION) {
                        f32 v = t->numeric_value;
                        advance();
                        return v;
                    }
                    return {};
                };
                auto expect_punct = [&](char c) -> bool {
                    auto* t = next_non_ws();
                    if (t->type == CssTokenType::DELIM && !t->text.empty() && t->text[0] == c) {
                        advance();
                        return true;
                    }
                    // Also accept comma token
                    if (t->type == CssTokenType::COMMA && c == ',') {
                        advance();
                        return true;
                    }
                    return false;
                };
                if (func_name == "rgb" || func_name == "rgba") {
                    auto r = expect_num(); if (!r || !expect_punct(',')) return {};
                    auto g = expect_num(); if (!g || !expect_punct(',')) return {};
                    auto b = expect_num(); if (!b) return {};
                    f32 a = 1.0f;
                    if (func_name == "rgba") {
                        if (!expect_punct(',')) return {};
                        auto av = expect_num(); if (!av) return {};
                        a = *av / 255.0f;
                    }
                    if (!expect_punct(')')) return {};
                    u8 ri = static_cast<u8>(std::max(0.0f, std::min(255.0f, *r)));
                    u8 gi = static_cast<u8>(std::max(0.0f, std::min(255.0f, *g)));
                    u8 bi = static_cast<u8>(std::max(0.0f, std::min(255.0f, *b)));
                    u8 ai = static_cast<u8>(std::max(0.0f, std::min(255.0f, a * 255.0f)));
                    return Color{ri, gi, bi, ai};
                }
                if (func_name == "hsl" || func_name == "hsla") {
                    // Simplified HSL→RGB conversion
                    auto h = expect_num(); if (!h || !expect_punct(',')) return {};
                    auto s = expect_num(); if (!s || !expect_punct(',')) return {};
                    auto l = expect_num(); if (!l) return {};
                    f32 a = 1.0f;
                    if (func_name == "hsla") {
                        if (!expect_punct(',')) return {};
                        auto av = expect_num(); if (!av) return {};
                        a = *av;
                    }
                    if (!expect_punct(')')) return {};
                    f32 hh = *h / 360.0f;
                    f32 ss = *s / 100.0f;
                    f32 ll = *l / 100.0f;
                    auto hue2rgb = [](f32 p, f32 q, f32 t) -> f32 {
                        if (t < 0) t += 1;
                        if (t > 1) t -= 1;
                        if (t < 1.0f/6) return p + (q - p) * 6 * t;
                        if (t < 1.0f/2) return q;
                        if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
                        return p;
                    };
                    f32 q = ll < 0.5f ? ll * (1 + ss) : ll + ss - ll * ss;
                    f32 p = 2 * ll - q;
                    u8 ri = static_cast<u8>(hue2rgb(p, q, hh + 1.0f/3) * 255);
                    u8 gi = static_cast<u8>(hue2rgb(p, q, hh) * 255);
                    u8 bi = static_cast<u8>(hue2rgb(p, q, hh - 1.0f/3) * 255);
                    u8 ai = static_cast<u8>(std::max(0.0f, std::min(255.0f, a * 255.0f)));
                    return Color{ri, gi, bi, ai};
                }
                return {};
            };
            auto color_opt = parse_color_args();
            if (color_opt.has_value()) {
                val.type = CSSValue::Type::COLOR;
                val.color = color_opt.value();
            } else {
                // Skip the function body to keep parser in sync
                u32 depth = 1;
                while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                    if (current_.type == CssTokenType::OPEN_PAREN) depth++;
                    if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
                    if (depth > 0) advance();
                }
                if (current_.type == CssTokenType::CLOSE_PAREN) advance();
            }
        } else {
            val.type = CSSValue::Type::FUNCTION;
            val.keyword = func_name;
            val.string_value = func_name;
            u32 depth = 1;
            while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::OPEN_PAREN) depth++;
                if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
                if (depth > 0) {
                    if (!val.string_value.empty() && val.string_value.back() != '(') val.string_value += ' ';
                    val.string_value += current_.text;
                    advance();
                }
            }
            if (current_.type == CssTokenType::CLOSE_PAREN) {
                val.string_value += ')';
                advance();
            }
        }
    } else if (current_.type == CssTokenType::HASH) {
        val.type = CSSValue::Type::COLOR;
        val.color = Color::from_hex("#" + current_.text);
        advance();
    } else {
        advance();
    }

    return val;
}

AtRule CssParser::parse_at_rule() {
    AtRule at;
    at.name = current_.text;
    advance();

    while (current_.type == CssTokenType::WHITESPACE) advance();

    while (current_.type != CssTokenType::OPEN_BRACE &&
           current_.type != CssTokenType::SEMICOLON &&
           current_.type != CssTokenType::EOF_TOKEN &&
           current_.type != CssTokenType::CLOSE_BRACE) {
        if (current_.type != CssTokenType::WHITESPACE) {
            if (!at.prelude.empty()) at.prelude += ' ';
            at.prelude += current_.text;
        }
        advance();
    }

    if (current_.type == CssTokenType::OPEN_BRACE) {
        advance();

        if (at.name == "font-face") {
            while (current_.type != CssTokenType::CLOSE_BRACE &&
                   current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::WHITESPACE) {
                    advance();
                    continue;
                }
                if (current_.type == CssTokenType::SEMICOLON) {
                    advance();
                    continue;
                }
                if (current_.type == CssTokenType::IDENT) {
                    at.declarations.push_back(parse_declaration());
                } else {
                    advance();
                }
            }
        } else {
            while (current_.type != CssTokenType::CLOSE_BRACE &&
                   current_.type != CssTokenType::EOF_TOKEN) {
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
        if (current_.type == CssTokenType::CLOSE_BRACE) advance();
    }

    if (current_.type == CssTokenType::SEMICOLON) advance();

    return at;
}

}
