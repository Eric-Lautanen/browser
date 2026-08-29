#include "shorthand_border.hpp"
#include "shorthand_box.hpp"

namespace browser::css {

void expand_border(ComputedStyle& style, const CSSValue& val, const std::string& prop) {
    auto expand_border_side = [&](const std::string &side, const CSSValue &bval) {
        if (!style.has(side + "-width") && !style.has("border-width"))
            style.properties[side + "-width"] = bval;
    };
    if (prop == "border" && val.type == CSSValue::Type::STRING) {
        expand_border_side("border-top", val);
        expand_border_side("border-right", val);
        expand_border_side("border-bottom", val);
        expand_border_side("border-left", val);
    }
    if (prop == "border-width" && val.type == CSSValue::Type::STRING) {
        std::string tmp = val.string_value;
        expand_four_sides(style, "borderwidth", tmp);
        for (auto &side : {"-top", "-right", "-bottom", "-left"}) {
            std::string old_key = "borderwidth" + std::string(side);
            std::string new_key = "border" + std::string(side) + "-width";
            auto it = style.properties.find(old_key);
            if (it != style.properties.end()) {
                style.properties[new_key] = it->second;
                style.properties.erase(it);
            }
        }
    }
    if (prop == "border-style" && val.type == CSSValue::Type::STRING) {
        // 4-value expansion for border-style
        expand_four_sides(style, "border-style", val.string_value);
    }
}

} // namespace browser::css
