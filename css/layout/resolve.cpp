#include "../layout.hpp"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace browser::css {

    f32 LayoutEngine::resolve_clamp_func(const std::string &expr, f32 parent_value, f32 font_size) const {
        std::string s = expr;
        size_t paren = s.find('(');
        if (paren == std::string::npos)
            return 0;
        std::string args_str = s.substr(paren + 1);
        if (!args_str.empty() && args_str.back() == ')')
            args_str.pop_back();

        std::vector<std::string> args;
        std::string cur;
        int depth = 0;
        for (char c : args_str) {
            if (c == '(') {
                depth++;
                cur += c;
            } else if (c == ')') {
                depth--;
                cur += c;
            } else if (c == ',' && depth == 0) {
                while (!cur.empty() && cur[0] == ' ') cur = cur.substr(1);
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                args.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        while (!cur.empty() && cur[0] == ' ') cur = cur.substr(1);
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        if (!cur.empty())
            args.push_back(cur);

        auto eval_arg = [&](const std::string &a) -> f32 {
            if (a.empty())
                return 0;
            if (a.substr(0, 5) == "calc(" || a.find("calc(") != std::string::npos) {
                return resolve_calc_string(a, parent_value, font_size);
            }
            char *end = nullptr;
            f32 val = std::strtof(a.c_str(), &end);
            if (end && end != a.c_str()) {
                std::string unit = end;
                while (!unit.empty() && unit[0] == ' ') unit = unit.substr(1);
                if (unit == "px")
                    return val;
                if (unit == "em")
                    return val * font_size;
                if (unit == "rem")
                    return val * root_font_size_;
                if (unit == "%")
                    return val / 100.0f * parent_value;
                if (unit == "vw")
                    return val / 100.0f * viewport_width_;
                if (unit == "vh")
                    return val / 100.0f * viewport_height_;
                return val;
            }
            return 0;
        };

        std::string func_name;
        {
            std::string temp = expr;
            size_t p = temp.find('(');
            if (p != std::string::npos)
                func_name = temp.substr(0, p);
        }

        if (func_name == "clamp" && args.size() >= 3) {
            f32 min_val = eval_arg(args[0]);
            f32 val = eval_arg(args[1]);
            f32 max_val = eval_arg(args[2]);
            return std::max(min_val, std::min(val, max_val));
        } else if (func_name == "min" && !args.empty()) {
            f32 result = eval_arg(args[0]);
            for (size_t i = 1; i < args.size(); i++) {
                result = std::min(result, eval_arg(args[i]));
            }
            return result;
        } else if (func_name == "max" && !args.empty()) {
            f32 result = eval_arg(args[0]);
            for (size_t i = 1; i < args.size(); i++) {
                result = std::max(result, eval_arg(args[i]));
            }
            return result;
        }
        return 0;
    }

    f32 LayoutEngine::resolve_font_size(const ComputedStyle &style, f32 parent_font_size) const {
        auto *v = style.get("font-size");
        if (!v)
            return parent_font_size;

        if (v->type == CSSValue::Type::LENGTH) {
            switch (v->length.unit) {
                case Length::Unit::EM:
                    return v->length.value * parent_font_size;
                case Length::Unit::REM:
                    return v->length.value * root_font_size_;
                case Length::Unit::PX:
                    return v->length.value;
                default:
                    return parent_font_size;
            }
        }

        if (v->type == CSSValue::Type::FUNCTION &&
            (v->keyword == "clamp" || v->keyword == "min" || v->keyword == "max")) {
            return resolve_clamp_func(v->string_value, parent_font_size, parent_font_size);
        }

        if (v->type == CSSValue::Type::KEYWORD) {
            if (v->keyword == "small")
                return 13.0f;
            if (v->keyword == "medium")
                return 16.0f;
            if (v->keyword == "large")
                return 18.0f;
            if (v->keyword == "x-large")
                return 24.0f;
            if (v->keyword == "xx-large")
                return 32.0f;
        }

        return parent_font_size;
    }

    f32 LayoutEngine::resolve_calc_string(const std::string &expr, f32 parent_value, f32 font_size) const {
        std::string s = expr;
        std::size_t start = s.find("calc(");
        if (start != std::string::npos) {
            s = s.substr(start + 5);
        }
        if (!s.empty() && s.back() == ')')
            s.pop_back();

        const char *p = s.c_str();

        auto skip_ws = [&]() {
            while (*p == ' ' || *p == '\t') p++;
        };

        auto read_number_with_unit = [&]() -> f32 {
            skip_ws();
            if (*p == '\0')
                return 0;
            char *end = nullptr;
            f32 num = static_cast<f32>(std::strtof(p, &end));
            if (end && end != p) {
                p = end;
                skip_ws();
                std::string unit;
                while (*p &&
                       !(*p == '+' || *p == '-' || *p == '*' || *p == '/' || *p == ')' || *p == ' ' || *p == '\t')) {
                    unit += *p;
                    p++;
                }
                if (unit == "px")
                    return num;
                if (unit == "%")
                    return num / 100.0f * parent_value;
                if (unit == "em")
                    return num * font_size;
                if (unit == "rem")
                    return num * root_font_size_;
                if (unit == "vw")
                    return num / 100.0f * viewport_width_;
                if (unit == "vh")
                    return num / 100.0f * viewport_height_;
                return num;
            }
            return 0;
        };

        std::function<f32()> parse_add_expr;
        std::function<f32()> parse_mul_expr;
        std::function<f32()> parse_unary_expr;
        std::function<f32()> parse_primary;

        parse_add_expr = [&]() -> f32 {
            f32 left = parse_mul_expr();
            while (true) {
                skip_ws();
                if (*p == '+') {
                    p++;
                    f32 right = parse_mul_expr();
                    left += right;
                } else if (*p == '-') {
                    p++;
                    f32 right = parse_mul_expr();
                    left -= right;
                } else
                    break;
            }
            return left;
        };

        parse_mul_expr = [&]() -> f32 {
            f32 left = parse_unary_expr();
            while (true) {
                skip_ws();
                if (*p == '*') {
                    p++;
                    f32 right = parse_unary_expr();
                    left *= right;
                } else if (*p == '/') {
                    p++;
                    f32 right = parse_unary_expr();
                    if (right != 0)
                        left /= right;
                } else
                    break;
            }
            return left;
        };

        parse_unary_expr = [&]() -> f32 {
            skip_ws();
            if (*p == '-') {
                p++;
                return -parse_unary_expr();
            }
            return parse_primary();
        };

        parse_primary = [&]() -> f32 {
            skip_ws();
            if (*p == '(') {
                p++;
                f32 val = parse_add_expr();
                skip_ws();
                if (*p == ')')
                    p++;
                return val;
            }
            return read_number_with_unit();
        };

        return parse_add_expr();
    }

    f32 LayoutEngine::resolve_length(const Length &len, f32 parent_value, f32 font_size) const {
        switch (len.unit) {
            case Length::Unit::PX:
                return len.value;
            case Length::Unit::EM:
                return len.value * font_size;
            case Length::Unit::REM:
                return len.value * root_font_size_;
            case Length::Unit::PERCENT:
                return len.value / 100.0f * parent_value;
            case Length::Unit::VW:
                return len.value / 100.0f * viewport_width_;
            case Length::Unit::VH:
                return len.value / 100.0f * viewport_height_;
            case Length::Unit::NONE:
                return 0.0f;
            case Length::Unit::DEG:
                return len.value;
            case Length::Unit::S:
                return len.value * 1000.0f;
            case Length::Unit::MS:
                return len.value;
        }
        return 0.0f;
    }

    f32 LayoutEngine::resolve_func_length(const ComputedStyle &,
                                          const CSSValue *v,
                                          f32 parent_value,
                                          f32 font_size) const {
        if (!v)
            return 0;
        if (v->type == CSSValue::Type::LENGTH) {
            return resolve_length(v->length, parent_value, font_size);
        }
        if (v->type == CSSValue::Type::FUNCTION &&
            (v->keyword == "clamp" || v->keyword == "min" || v->keyword == "max")) {
            return resolve_clamp_func(v->string_value, parent_value, font_size);
        }
        if (v->type == CSSValue::Type::STRING && !v->string_value.empty()) {
            std::string s = v->string_value;
            if (s.substr(0, 5) == "calc(" || s.find("calc(") != std::string::npos) {
                return resolve_calc_string(s, parent_value, font_size);
            }
            if (s.substr(0, 6) == "clamp(" || s.substr(0, 4) == "min(" || s.substr(0, 4) == "max(") {
                return resolve_clamp_func(s, parent_value, font_size);
            }
        }
        return 0;
    }

    f32 LayoutEngine::resolve_side_value(const ComputedStyle &style,
                                         const std::string &side_prop,
                                         const std::string &shorthand_prop,
                                         f32 containing,
                                         f32 font_size) const {
        auto *v = style.get(side_prop);
        if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto")) {
            v = style.get(shorthand_prop);
        }
        if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto"))
            return 0.0f;
        if (v->type == CSSValue::Type::LENGTH) {
            return resolve_length(v->length, containing, font_size);
        }
        if (v->type == CSSValue::Type::STRING && v->string_value.find("calc(") != std::string::npos) {
            return resolve_calc_string(v->string_value, containing, font_size);
        }
        return 0.0f;
    }

    f32 LayoutEngine::resolve_property(const ComputedStyle &style,
                                       const std::string &prop,
                                       f32 containing,
                                       f32 font_size) const {
        auto *v = style.get(prop);
        if (!v || (v->type == CSSValue::Type::KEYWORD && v->keyword == "auto"))
            return 0.0f;
        if (v->type == CSSValue::Type::LENGTH) {
            return resolve_length(v->length, containing, font_size);
        }
        if (v->type == CSSValue::Type::STRING && !v->string_value.empty() &&
            v->string_value.find("calc(") != std::string::npos) {
            return resolve_calc_string(v->string_value, containing, font_size);
        }
        return 0.0f;
    }

}  // namespace browser::css
