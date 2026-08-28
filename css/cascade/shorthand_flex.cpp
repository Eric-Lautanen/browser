#include "shorthand_flex.hpp"
#include <cstdlib>
#include <vector>

namespace browser::css {

void expand_flex(ComputedStyle& style, const CSSValue& val) {
    if (val.type != CSSValue::Type::STRING) return;
    std::string s = val.string_value;
    std::string trimmed = s;
    while (!trimmed.empty() && trimmed[0] == ' ') trimmed = trimmed.substr(1);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    if (trimmed == "none") {
        auto set_flex_val = [&](const std::string &subprop, f32 num) {
            CSSValue cv; cv.type = CSSValue::Type::NUMBER; cv.number = num;
            style.properties[subprop] = cv;
        };
        set_flex_val("flex-grow", 0);
        set_flex_val("flex-shrink", 0);
        CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = "auto";
        style.properties["flex-basis"] = cv;
        return;
    }
    std::vector<std::string> parts;
    size_t pp = 0;
    while (pp < s.size()) {
        while (pp < s.size() && s[pp] == ' ') pp++;
        if (pp >= s.size()) break;
        size_t end = s.find(' ', pp);
        if (end == std::string::npos) end = s.size();
        parts.push_back(s.substr(pp, end - pp));
        pp = end + 1;
    }
    auto set_flex_num = [&](const std::string &subprop, const std::string &pv) {
        CSSValue cv; char *endp = nullptr; f32 num = std::strtof(pv.c_str(), &endp);
        if (endp != pv.c_str()) { cv.type = CSSValue::Type::NUMBER; cv.number = num; style.properties[subprop] = cv; }
    };
    auto set_flex_basis = [&](const std::string &pv) {
        CSSValue cv; char *endp = nullptr; f32 num = std::strtof(pv.c_str(), &endp);
        if (endp != pv.c_str()) {
            cv.type = CSSValue::Type::LENGTH; cv.length.value = num;
            std::string unit = endp;
            if (unit == "px") cv.length.unit = Length::Unit::PX;
            else if (unit == "em") cv.length.unit = Length::Unit::EM;
            else if (unit == "rem") cv.length.unit = Length::Unit::REM;
            else if (unit == "%") cv.length.unit = Length::Unit::PERCENT;
            else { cv.type = CSSValue::Type::KEYWORD; cv.keyword = pv; }
        } else { cv.type = CSSValue::Type::KEYWORD; cv.keyword = pv; }
        style.properties["flex-basis"] = cv;
    };
    if (parts.size() >= 1) set_flex_num("flex-grow", parts[0]);
    if (parts.size() >= 2) set_flex_num("flex-shrink", parts[1]);
    if (parts.size() >= 3) set_flex_basis(parts[2]);
    if (parts.size() == 1) {
        CSSValue cv; cv.type = CSSValue::Type::NUMBER; cv.number = 1; style.properties["flex-shrink"] = cv;
        cv.type = CSSValue::Type::LENGTH; cv.length = {0, Length::Unit::PX}; style.properties["flex-basis"] = cv;
    }
    if (parts.size() == 2) {
        CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = "auto";
        if (!style.has("flex-basis")) style.properties["flex-basis"] = cv;
    }
}

} // namespace browser::css
