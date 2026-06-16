#include "painter.hpp"
#include <cmath>

namespace browser::render {

Painter::Painter(TextRenderer* tr)
    : text_renderer_(tr) {}

void Painter::paint(css::LayoutNode* root) {
    list_.clear();
    if (root) paint_node(root, 0, 0);
}

const DisplayList& Painter::display_list() const {
    return list_;
}

void Painter::paint_node(css::LayoutNode* node, f32 ox, f32 oy) {
    if (!node) return;

    paint_background(node, ox, oy);
    paint_border(node, ox, oy);

    // Check overflow property — only clip for overflow:hidden/scroll/auto
    bool has_clip = false;
    auto* overflow = node->style().get("overflow");
    if (overflow && overflow->type == css::CSSValue::Type::KEYWORD) {
        has_clip = (overflow->keyword == "hidden" || overflow->keyword == "scroll" ||
                    overflow->keyword == "auto");
    }
    if (!has_clip) {
        auto* ox = node->style().get("overflow-x");
        if (ox && ox->type == css::CSSValue::Type::KEYWORD) {
            has_clip = (ox->keyword == "hidden" || ox->keyword == "scroll" ||
                        ox->keyword == "auto");
        }
    }
    if (!has_clip) {
        auto* oy = node->style().get("overflow-y");
        if (oy && oy->type == css::CSSValue::Type::KEYWORD) {
            has_clip = (oy->keyword == "hidden" || oy->keyword == "scroll" ||
                        oy->keyword == "auto");
        }
    }

    if (has_clip) {
        f32 px = ox - node->padding.left;
        f32 py = oy - node->padding.top;
        f32 pw = node->content.width + node->padding.left + node->padding.right;
        f32 ph = node->content.height + node->padding.top + node->padding.bottom;
        list_.push({PaintCommand::Type::PUSH_CLIP, {px, py, pw, ph}, Color::TRANSPARENT, "", 0});
    }

    // Emit text before children (CSS paint order: bg → border → content → children)
    if (node->is_text()) {
        paint_text(node, ox, oy);
    }

    for (auto& child : node->children) {
        paint_node(child.get(), ox + child->content.x, oy + child->content.y);
    }

    if (has_clip) {
        list_.push({PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT, "", 0});
    }
}

void Painter::paint_background(css::LayoutNode* node, f32 ox, f32 oy) {
    Color bg = resolve_color(node->style(), "background-color", Color::TRANSPARENT);
    if (bg.a == 0.0f) return;

    f32 bx = ox - node->padding.left;
    f32 by = oy - node->padding.top;
    f32 bw = node->content.width + node->padding.left + node->padding.right
             + node->border.left + node->border.right;
    f32 bh = node->content.height + node->padding.top + node->padding.bottom
             + node->border.top + node->border.bottom;
    list_.push({PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, bg, "", 0});
}

void Painter::paint_border(css::LayoutNode* node, f32 ox, f32 oy) {
    auto& b = node->border;
    if (b.top == 0.0f && b.right == 0.0f && b.bottom == 0.0f && b.left == 0.0f) return;

    Color border_color = resolve_color_fallback(node->style(),
        {"border-top-color", "border-color"}, Color::BLACK);
    Color border_right = resolve_color_fallback(node->style(),
        {"border-right-color", "border-color"}, Color::BLACK);
    Color border_bottom = resolve_color_fallback(node->style(),
        {"border-bottom-color", "border-color"}, Color::BLACK);
    Color border_left = resolve_color_fallback(node->style(),
        {"border-left-color", "border-color"}, Color::BLACK);

    // Border box (includes padding + border)
    f32 bx = ox - node->padding.left - b.left;
    f32 by = oy - node->padding.top - b.top;
    f32 bw = node->content.width + node->padding.left + node->padding.right
             + b.left + b.right;
    f32 bh = node->content.height + node->padding.top + node->padding.bottom
             + b.top + b.bottom;

    // Top border
    if (b.top > 0.0f) {
        list_.push({PaintCommand::Type::FILL_RECT,
            {bx, by, bw, b.top}, border_color, "", 0});
    }

    // Right border
    if (b.right > 0.0f) {
        list_.push({PaintCommand::Type::FILL_RECT,
            {bx + bw - b.right, by, b.right, bh}, border_right, "", 0});
    }

    // Bottom border
    if (b.bottom > 0.0f) {
        list_.push({PaintCommand::Type::FILL_RECT,
            {bx, by + bh - b.bottom, bw, b.bottom}, border_bottom, "", 0});
    }

    // Left border
    if (b.left > 0.0f) {
        list_.push({PaintCommand::Type::FILL_RECT,
            {bx, by, b.left, bh}, border_left, "", 0});
    }
}

void Painter::paint_text(css::LayoutNode* node, f32 ox, f32 oy) {
    Color text_color = resolve_color(node->style(), "color", Color::BLACK);
    f32 font_size = resolve_font_size(node->style());
    // Extend height by descender allowance so p/q/g/y aren't clipped by
    // ancestor overflow:hidden clips sized to the ascender-only content box.
    f32 descender_pad = std::ceil(font_size * 0.25f);
    list_.push({PaintCommand::Type::DRAW_TEXT,
        {ox, oy, node->content.width, node->content.height + descender_pad},
        text_color, node->text(), font_size});
}

Color Painter::resolve_color(const css::ComputedStyle& style,
                              const std::string& prop,
                              const Color& fallback) const
{
    auto* v = style.get(prop);
    if (!v) return fallback;

    if (v->type == css::CSSValue::Type::COLOR) {
        return css_to_render_color(v->color);
    }

    if (v->type == css::CSSValue::Type::KEYWORD) {
        if (v->keyword == "inherit" && style.parent) {
            return resolve_color(*style.parent, prop, fallback);
        }
        auto css_c = css::Color::from_name(v->keyword);
        return css_to_render_color(css_c);
    }

    return fallback;
}

Color Painter::resolve_color_fallback(const css::ComputedStyle& style,
                                       const std::vector<std::string>& props,
                                       const Color& fallback) const
{
    for (const auto& prop : props) {
        if (style.get(prop)) {
            return resolve_color(style, prop, fallback);
        }
    }
    return fallback;
}

// TODO: share resolve_font_size logic with LayoutEngine (layout.cpp)
f32 Painter::resolve_font_size(const css::ComputedStyle& style) const {
    auto* v = style.get("font-size");
    if (!v) return 16.0f;

    if (v->type == css::CSSValue::Type::LENGTH) {
        switch (v->length.unit) {
            case css::Length::Unit::PX:  return v->length.value;
            case css::Length::Unit::EM:  return v->length.value * 16.0f;
            case css::Length::Unit::REM: return v->length.value * 16.0f;
            default: return 16.0f;
        }
    }

    if (v->type == css::CSSValue::Type::KEYWORD) {
        if (v->keyword == "small")    return 13.0f;
        if (v->keyword == "medium")   return 16.0f;
        if (v->keyword == "large")    return 18.0f;
        if (v->keyword == "x-large")  return 24.0f;
        if (v->keyword == "xx-large") return 32.0f;
    }

    return 16.0f;
}

} // namespace browser::render
