#pragma once
#include "commands.hpp"
#include "../../css/css_values.hpp"
#include "../../css/layout/types.hpp"

namespace browser::render {

inline PaintCommand make_cmd(PaintCommand::Type type,
                             css::Rect rect,
                             Color color,
                             const std::string &text = "",
                             float font_size = 16,
                             ImageId image_id = 0,
                             const css::CSSGradient &gradient = {},
                             float radius = 0,
                             const css::Mat3x3 &transform = {},
                             float opacity = 1.0f,
                             uint8_t font_flags = 0,
                             uint8_t image_flags = 0) {
    PaintCommand cmd;
    cmd.type = type;
    cmd.rect = rect;
    cmd.color = color;
    cmd.text = text;
    cmd.font_size = font_size;
    cmd.font_flags = font_flags;
    cmd.image_id = image_id;
    cmd.gradient = gradient;
    cmd.radius = radius;
    cmd.transform = transform;
    cmd.opacity = opacity;
    cmd.image_flags = image_flags;
    return cmd;
}

inline Color resolve_color(const css::ComputedStyle &style, const std::string &prop, Color fallback) {
    auto *v = style.get(prop);
    if (!v) {
        if (style.parent) return resolve_color(*style.parent, prop, fallback);
        return fallback;
    }
    if (v->type == css::CSSValue::Type::COLOR) return Color{v->color.r / 255.0f, v->color.g / 255.0f, v->color.b / 255.0f, v->color.a / 255.0f};
    if (v->type == css::CSSValue::Type::KEYWORD && v->keyword == "transparent") return Color::TRANSPARENT;
    return fallback;
}

inline Color resolve_color_fallback(const css::ComputedStyle &style, std::initializer_list<std::string> props, Color fallback) {
    for (auto &p : props) {
        Color c = resolve_color(style, p, fallback);
        if (c.a != fallback.a || c.r != fallback.r || c.g != fallback.g || c.b != fallback.b) return c;
        auto *v = style.get(p);
        if (v) return c;
    }
    return fallback;
}

} // namespace browser::render
