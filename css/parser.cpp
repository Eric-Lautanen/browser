#include "parser.hpp"
#include <cctype>
#include <optional>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace browser::css {

CssParser::CssParser(const std::string& input) : tokenizer_(input) {
    advance();
}

void CssParser::advance() {
    current_ = tokenizer_.next();
}

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

std::string CssParser::consume_function_body() {
    std::string result;
    u32 depth = 1;
    while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
        if (current_.type == CssTokenType::OPEN_PAREN) depth++;
        if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
        if (depth > 0) {
            if (!result.empty() && current_.type != CssTokenType::CLOSE_PAREN) result += ' ';
            if (current_.type != CssTokenType::OPEN_PAREN || depth > 1) {
                if (current_.type == CssTokenType::DELIM) result += current_.text;
                else if (current_.type == CssTokenType::IDENT) result += current_.text;
                else if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION || current_.type == CssTokenType::PERCENTAGE) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.2f", current_.numeric_value);
                    std::string num = buf;
                    auto dot = num.find('.');
                    if (dot != std::string::npos) {
                        auto last = num.find_last_not_of('0');
                        if (last > dot) num = num.substr(0, last + 1);
                        else if (last == dot) num = num.substr(0, dot);
                    }
                    result += num;
                    if (current_.type == CssTokenType::DIMENSION) result += current_.text;
                    else if (current_.type == CssTokenType::PERCENTAGE) result += '%';
                }
                else if (current_.type == CssTokenType::STRING) result += '\"' + current_.text + '\"';
                else if (current_.type == CssTokenType::URL) result += "url(" + current_.text + ")";
                else if (current_.type == CssTokenType::WHITESPACE) result += ' ';
                else if (current_.type == CssTokenType::COMMA) result += ", ";
                else if (current_.type == CssTokenType::COLON) result += ':';
                else if (current_.type == CssTokenType::SEMICOLON) result += ';';
                else if (current_.type == CssTokenType::AT_KEYWORD) result += '@' + current_.text;
                else if (current_.type == CssTokenType::FUNCTION) result += current_.text + '(';
                else if (current_.type == CssTokenType::HASH) result += '#' + current_.text;
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
                    if (current_.type == CssTokenType::OPEN_PAREN) depth++;
                    if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
                    if (depth > 0) advance();
                }
                if (current_.type == CssTokenType::CLOSE_PAREN) advance();
            }
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

CSSValue CssParser::parse_calc_args() {
    CSSValue val;
    val.type = CSSValue::Type::NUMBER;
    val.number = 0;
    val.string_value = "calc(";

    auto read_num = [&]() -> std::pair<f32, std::string> {
        f32 num = 0;
        std::string unit;
        if (current_.type == CssTokenType::NUMBER) {
            num = current_.numeric_value;
            advance();
        } else if (current_.type == CssTokenType::DIMENSION) {
            num = current_.numeric_value;
            unit = current_.text;
            for (auto& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            advance();
        } else if (current_.type == CssTokenType::PERCENTAGE) {
            num = current_.numeric_value;
            unit = "%";
            advance();
        } else if (current_.type == CssTokenType::OPEN_PAREN) {
            advance();
            auto inner = parse_calc_args();
            if (current_.type == CssTokenType::CLOSE_PAREN) advance();
            return {inner.number, inner.string_value};
        }
        return {num, unit};
    };

    std::string accumulated;
    f32 result = 0;
    std::string result_unit;

    auto flush_op = [&](f32 rhs, const std::string& unit, char op) {
        if (op == '+') result += rhs;
        else if (op == '-') result -= rhs;
        else if (op == '*') result *= rhs;
        else if (op == '/' && rhs != 0) result /= rhs;
        if (!unit.empty()) result_unit = unit;
    };

    auto [first_num, first_unit] = read_num();
    result = first_num;
    result_unit = first_unit;

    char last_op = 0;
    while (current_.type != CssTokenType::CLOSE_PAREN &&
           current_.type != CssTokenType::EOF_TOKEN &&
           current_.type != CssTokenType::SEMICOLON &&
           current_.type != CssTokenType::CLOSE_BRACE) {
        if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
        if (current_.type == CssTokenType::DELIM &&
            (current_.text == "+" || current_.text == "-" ||
             current_.text == "*" || current_.text == "/")) {
            last_op = current_.text[0];
            advance();
            continue;
        }
        auto [rhs_num, rhs_unit] = read_num();
        if (last_op) {
            flush_op(rhs_num, rhs_unit.empty() ? result_unit : rhs_unit, last_op);
            last_op = 0;
        }
    }

    val.number = result;
    if (!result_unit.empty()) {
        val.type = CSSValue::Type::LENGTH;
        if (result_unit == "px") val.length = {result, Length::Unit::PX};
        else if (result_unit == "em") val.length = {result, Length::Unit::EM};
        else if (result_unit == "rem") val.length = {result, Length::Unit::REM};
        else if (result_unit == "%") val.length = {result, Length::Unit::PERCENT};
        else if (result_unit == "vw") val.length = {result, Length::Unit::VW};
        else if (result_unit == "vh") val.length = {result, Length::Unit::VH};
        else val.length = {result, Length::Unit::NONE};
    } else {
        val.type = CSSValue::Type::NUMBER;
    }

    return val;
}

CSSValue CssParser::parse_transform_func(const std::string& func_name) {
    CSSValue val;
    val.type = CSSValue::Type::TRANSFORM;

    auto read_num = [&]() -> f32 {
        f32 n = 0;
        while (current_.type == CssTokenType::WHITESPACE) advance();
        if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION) {
            n = current_.numeric_value;
            advance();
        }
        return n;
    };

    auto skip_punct = [&]() {
        while (current_.type == CssTokenType::WHITESPACE || current_.type == CssTokenType::COMMA) advance();
    };

    TransformFunc tf;
    std::vector<f32> args;

    if (func_name == "matrix") {
        tf.type = TransformFunc::Type::MATRIX;
        for (int i = 0; i < 6; i++) {
            if (i > 0) skip_punct();
            args.push_back(read_num());
        }
    } else if (func_name == "translate") {
        tf.type = TransformFunc::Type::TRANSLATE;
        args.push_back(read_num());
        skip_punct();
        f32 y = read_num();
        args.push_back(y);
    } else if (func_name == "translatex") {
        tf.type = TransformFunc::Type::TRANSLATE_X;
        args.push_back(read_num());
    } else if (func_name == "translatey") {
        tf.type = TransformFunc::Type::TRANSLATE_Y;
        args.push_back(read_num());
    } else if (func_name == "rotate") {
        tf.type = TransformFunc::Type::ROTATE;
        args.push_back(read_num());
    } else if (func_name == "scale") {
        tf.type = TransformFunc::Type::SCALE;
        args.push_back(read_num());
        skip_punct();
        f32 sy = read_num();
        if (current_.type != CssTokenType::CLOSE_PAREN && sy != 0) args.push_back(sy);
    } else if (func_name == "scalex") {
        tf.type = TransformFunc::Type::SCALE_X;
        args.push_back(read_num());
    } else if (func_name == "scaley") {
        tf.type = TransformFunc::Type::SCALE_Y;
        args.push_back(read_num());
    } else if (func_name == "skew") {
        tf.type = TransformFunc::Type::SKEW;
        args.push_back(read_num());
        skip_punct();
        args.push_back(read_num());
    } else if (func_name == "skewx") {
        tf.type = TransformFunc::Type::SKEW_X;
        args.push_back(read_num());
    } else if (func_name == "skewy") {
        tf.type = TransformFunc::Type::SKEW_Y;
        args.push_back(read_num());
    }

    tf.args = args;
    val.transforms.push_back(tf);

    while (current_.type == CssTokenType::WHITESPACE) advance();
    if (current_.type == CssTokenType::CLOSE_PAREN) advance();

    return val;
}

CSSValue CssParser::parse_gradient(const std::string& func_name) {
    CSSValue val;
    val.type = CSSValue::Type::GRADIENT;

    CSSGradient grad;

    if (func_name == "repeating-linear-gradient") { grad.type = CSSGradient::Type::REPEATING_LINEAR; }
    else if (func_name == "repeating-radial-gradient") { grad.type = CSSGradient::Type::REPEATING_RADIAL; }
    else if (func_name == "repeating-conic-gradient") { grad.type = CSSGradient::Type::REPEATING_CONIC; }
    else if (func_name.find("linear") != std::string::npos) grad.type = CSSGradient::Type::LINEAR;
    else if (func_name.find("radial") != std::string::npos) grad.type = CSSGradient::Type::RADIAL;
    else if (func_name.find("conic") != std::string::npos) grad.type = CSSGradient::Type::CONIC;

    auto skip_ws_punct = [&]() {
        while (true) {
            if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
            if (current_.type == CssTokenType::COMMA) { advance(); continue; }
            break;
        }
    };

    auto read_color_stop = [&]() -> std::optional<CSSGradientStop> {
        skip_ws_punct();
        CSSGradientStop stop;

        // Read color
        if (current_.type == CssTokenType::HASH) {
            stop.color = Color::from_hex("#" + current_.text);
            advance();
        } else if (current_.type == CssTokenType::IDENT) {
            stop.color = Color::from_name(current_.text);
            auto named = Color::from_name(current_.text);
            stop.color = named;
            advance();
        } else if (current_.type == CssTokenType::FUNCTION) {
            std::string fn = current_.text;
            for (auto& c : fn) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            advance();
            auto parse_comma_num = [&]() -> f32 {
                skip_ws_punct();
                f32 v = 0;
                if (current_.type == CssTokenType::NUMBER) { v = current_.numeric_value; advance(); }
                return v;
            };
            if (fn == "rgb" || fn == "rgba") {
                u8 r = (u8)parse_comma_num(), g = (u8)parse_comma_num(), b = (u8)parse_comma_num();
                u8 a = 255;
                if (fn == "rgba") { skip_ws_punct(); a = (u8)parse_comma_num(); }
                stop.color = {r, g, b, a};
            }
            skip_ws_punct();
            if (current_.type == CssTokenType::CLOSE_PAREN) advance();
        }

        // Read optional position
        skip_ws_punct();
        if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION || current_.type == CssTokenType::PERCENTAGE) {
            f32 pos = current_.numeric_value;
            if (current_.type == CssTokenType::PERCENTAGE) pos = current_.numeric_value / 100.0f;
            else if (current_.type == CssTokenType::DIMENSION) {
                std::string unit;
                for (char c : current_.text) unit += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (unit == "deg") pos = current_.numeric_value / 360.0f;
                else pos = current_.numeric_value / 100.0f;
            } else {
                pos = current_.numeric_value / 100.0f;
            }
            stop.position = std::max(0.0f, std::min(1.0f, pos));
            advance();
        }

        return stop;
    };

    // Check for angle or direction keyword
    skip_ws_punct();
    bool has_direction = false;
    if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION) {
        std::string unit;
        if (current_.type == CssTokenType::DIMENSION) {
            for (char c : current_.text) unit += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (unit == "deg") {
                grad.angle = current_.numeric_value;
                has_direction = true;
                advance();
                skip_ws_punct();
            } else if (unit == "rad") {
                grad.angle = current_.numeric_value * 180.0f / 3.14159265f;
                has_direction = true;
                advance();
            } else if (unit == "turn") {
                grad.angle = current_.numeric_value * 360.0f;
                has_direction = true;
                advance();
            }
        }
    }
    if (!has_direction && current_.type == CssTokenType::IDENT) {
        std::string kw = current_.text;
        for (auto& c : kw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (kw == "to") {
            advance();
            skip_ws_punct();
            if (current_.type == CssTokenType::IDENT) {
                std::string dir = current_.text;
                for (auto& c : dir) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (dir == "top") grad.angle = 0;
                else if (dir == "right") grad.angle = 90;
                else if (dir == "bottom") grad.angle = 180;
                else if (dir == "left") grad.angle = 270;
                advance();
                skip_ws_punct();
                if (current_.type == CssTokenType::IDENT) {
                    std::string dir2 = current_.text;
                    for (auto& c : dir2) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (dir2 == "top" && grad.angle == 270) grad.angle = 315;
                    else if (dir2 == "top" && grad.angle == 90) grad.angle = 45;
                    else if (dir2 == "bottom" && grad.angle == 270) grad.angle = 225;
                    else if (dir2 == "bottom" && grad.angle == 90) grad.angle = 135;
                    advance();
                }
                skip_ws_punct();
            }
        }
    }

    // If there was a comma after angle/direction, skip it
    if (has_direction && current_.type == CssTokenType::COMMA) advance();

    // Parse color stops
    while (current_.type != CssTokenType::CLOSE_PAREN && current_.type != CssTokenType::EOF_TOKEN) {
        auto stop = read_color_stop();
        if (stop.has_value()) {
            grad.stops.push_back(stop.value());
        } else {
            advance();
        }
    }

    if (current_.type == CssTokenType::CLOSE_PAREN) advance();

    val.gradient = grad;
    return val;
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
        else if (lower == "deg") val.length.unit = Length::Unit::DEG;
        else if (lower == "s") val.length.unit = Length::Unit::S;
        else if (lower == "ms") val.length.unit = Length::Unit::MS;
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

        // calc() — store as string for deferred evaluation at layout time
        if (func_name == "calc" || func_name == "-webkit-calc") {
            val.type = CSSValue::Type::STRING;
            val.string_value = "calc(";
            u32 depth = 1;
            while (depth > 0 && current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::OPEN_PAREN) depth++;
                if (current_.type == CssTokenType::CLOSE_PAREN) depth--;
                if (depth > 0) {
                    if (current_.type == CssTokenType::WHITESPACE) {
                        if (!val.string_value.empty() && val.string_value.back() != ' ') val.string_value += ' ';
                    } else if (current_.type == CssTokenType::DELIM) {
                        val.string_value += current_.text;
                    } else if (current_.type == CssTokenType::NUMBER || current_.type == CssTokenType::DIMENSION || current_.type == CssTokenType::PERCENTAGE) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.2f", current_.numeric_value);
                        std::string num = buf;
                        auto dot = num.find('.');
                        if (dot != std::string::npos) {
                            auto last = num.find_last_not_of('0');
                            if (last > dot) num = num.substr(0, last + 1);
                            else if (last == dot) num = num.substr(0, dot);
                        }
                        val.string_value += num;
                        if (current_.type == CssTokenType::DIMENSION) val.string_value += current_.text;
                        else if (current_.type == CssTokenType::PERCENTAGE) val.string_value += '%';
                    }
                    advance();
                }
            }
            if (current_.type == CssTokenType::CLOSE_PAREN) advance();
            val.string_value += ')';
            return val;
        }

        // var()
        if (func_name == "var") {
            val.type = CSSValue::Type::KEYWORD;
            val.keyword = "var(";
            while (current_.type != CssTokenType::CLOSE_PAREN && current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
                if (current_.type == CssTokenType::COMMA) { val.keyword += ", "; advance(); continue; }
                val.keyword += current_.text;
                advance();
            }
            val.keyword += ')';
            if (current_.type == CssTokenType::CLOSE_PAREN) advance();
            return val;
        }

        // Transform functions
        if (func_name == "matrix" || func_name == "translate" || func_name == "translatex" ||
            func_name == "translatey" || func_name == "rotate" || func_name == "scale" ||
            func_name == "scalex" || func_name == "scaley" || func_name == "skew" ||
            func_name == "skewx" || func_name == "skewy") {
            return parse_transform_func(func_name);
        }

        // Gradient functions
        if (func_name.find("gradient") != std::string::npos || func_name.find("-gradient") != std::string::npos) {
            return parse_gradient(func_name);
        }

        // rgb()/rgba()/hsl()/hsla() color functions
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
    } else if (current_.type == CssTokenType::DELIM && current_.text == "-") {
        advance();
        if (current_.type == CssTokenType::IDENT && current_.text.size() >= 2 &&
            current_.text[0] == '-') {
            // Custom property value or unknown dashed identifier
            val.type = CSSValue::Type::STRING;
            val.string_value = "-" + current_.text;
            advance();
        } else {
            val.type = CSSValue::Type::KEYWORD;
            val.keyword = "-" + current_.text;
        }
    } else {
        advance();
    }

    return val;
}

bool CssParser::parse_keyframe_block(KeyframeBlock& block) {
    // Parse keyframe selector: percentage or from/to
    while (current_.type != CssTokenType::OPEN_BRACE &&
           current_.type != CssTokenType::EOF_TOKEN &&
           current_.type != CssTokenType::CLOSE_BRACE) {
        if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
        if (current_.type == CssTokenType::COMMA) { advance(); continue; }

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
            for (auto& c : kw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (kw == "from") block.positions.push_back(0.0f);
            else if (kw == "to") block.positions.push_back(100.0f);
            advance();
        } else {
            advance();
        }
    }

    if (current_.type == CssTokenType::OPEN_BRACE) {
        advance();
    }

    while (current_.type != CssTokenType::CLOSE_BRACE &&
           current_.type != CssTokenType::EOF_TOKEN) {
        if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
        if (current_.type == CssTokenType::SEMICOLON) { advance(); continue; }
        block.declarations.push_back(parse_declaration());
        if (current_.type == CssTokenType::SEMICOLON) advance();
    }

    if (current_.type == CssTokenType::CLOSE_BRACE) advance();

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

    while (current_.type != CssTokenType::CLOSE_BRACE &&
           current_.type != CssTokenType::EOF_TOKEN) {
        if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
        KeyframeBlock block;
        if (parse_keyframe_block(block)) {
            kr.blocks.push_back(std::move(block));
        }
    }

    if (current_.type == CssTokenType::CLOSE_BRACE) advance();

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

        std::string lower_name;
        for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (lower_name == "font-face") {
            while (current_.type != CssTokenType::CLOSE_BRACE &&
                   current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
                if (current_.type == CssTokenType::SEMICOLON) { advance(); continue; }
                if (current_.type == CssTokenType::IDENT) {
                    at.declarations.push_back(parse_declaration());
                } else {
                    advance();
                }
            }
        } else {
            while (current_.type != CssTokenType::CLOSE_BRACE &&
                   current_.type != CssTokenType::EOF_TOKEN) {
                if (current_.type == CssTokenType::WHITESPACE) { advance(); continue; }
                if (current_.type == CssTokenType::SEMICOLON) { advance(); continue; }
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
