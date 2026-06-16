#include "painter.hpp"
#include <cmath>

namespace browser::render {

Painter::Painter(TextRenderer* tr)
    : text_renderer_(tr) {}

void Painter::set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>>& images) {
    images_ = &images;
}

async::task<std::shared_ptr<DisplayList>> Painter::paint(css::LayoutNode* root) {
    co_await async::thread_pool_executor{};
    auto list = std::make_shared<DisplayList>();
    if (root) paint_node(*list, root, 0, 0);
    co_return list;
}

void Painter::paint_node(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    if (!node) return;

    paint_background(list, node, ox, oy);
    paint_border(list, node, ox, oy);
    paint_image(list, node, ox, oy);

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
        list.push({PaintCommand::Type::PUSH_CLIP, {px, py, pw, ph}, Color::TRANSPARENT, "", 0});
    }

    if (node->is_text()) {
        paint_text(list, node, ox, oy);
    }

    for (auto& child : node->children) {
        paint_node(list, child.get(), ox + child->content.x, oy + child->content.y);
    }

    if (has_clip) {
        list.push({PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT, "", 0});
    }
}

void Painter::paint_background(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    Color bg = resolve_color(node->style(), "background-color", Color::TRANSPARENT);
    if (bg.a == 0.0f) return;

    f32 bx = ox - node->padding.left;
    f32 by = oy - node->padding.top;
    f32 bw = node->content.width + node->padding.left + node->padding.right
             + node->border.left + node->border.right;
    f32 bh = node->content.height + node->padding.top + node->padding.bottom
             + node->border.top + node->border.bottom;
    list.push({PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, bg, "", 0});
}

void Painter::paint_border(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
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

    f32 bx = ox - node->padding.left - b.left;
    f32 by = oy - node->padding.top - b.top;
    f32 bw = node->content.width + node->padding.left + node->padding.right
             + b.left + b.right;
    f32 bh = node->content.height + node->padding.top + node->padding.bottom
             + b.top + b.bottom;

    if (b.top > 0.0f) {
        list.push({PaintCommand::Type::FILL_RECT,
            {bx, by, bw, b.top}, border_color, "", 0});
    }

    if (b.right > 0.0f) {
        list.push({PaintCommand::Type::FILL_RECT,
            {bx + bw - b.right, by, b.right, bh}, border_right, "", 0});
    }

    if (b.bottom > 0.0f) {
        list.push({PaintCommand::Type::FILL_RECT,
            {bx, by + bh - b.bottom, bw, b.bottom}, border_bottom, "", 0});
    }

    if (b.left > 0.0f) {
        list.push({PaintCommand::Type::FILL_RECT,
            {bx, by, b.left, bh}, border_left, "", 0});
    }
}

void Painter::paint_text(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    Color text_color = resolve_color(node->style(), "color", Color::BLACK);
    f32 font_size = resolve_font_size(node->style());
    f32 descender_pad = std::ceil(font_size * 0.25f);
    list.push({PaintCommand::Type::DRAW_TEXT,
        {ox, oy, node->content.width, node->content.height + descender_pad},
        text_color, node->text(), font_size});
}

void Painter::paint_image(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    if (node->is_text()) return;
    html::Node* n = node->node();
    if (!n || n->type != html::NodeType::ELEMENT) return;
    auto* el = static_cast<html::Element*>(n);
    if (el->tag_name != "img") return;

    std::string src = el->get_attribute("src");
    if (src.empty() || !images_) return;

    auto it = images_->find(src);
    if (it == images_->end() || !it->second) return;

    auto* img = it->second.get();
    ImageId id = reinterpret_cast<ImageId>(img);
    f32 bx = ox;
    f32 by = oy;
    f32 bw = node->content.width > 0 ? node->content.width : static_cast<f32>(img->width);
    f32 bh = node->content.height > 0 ? node->content.height : static_cast<f32>(img->height);

    list.push({PaintCommand::Type::DRAW_IMAGE, {bx, by, bw, bh}, Color::WHITE, "", 0, id});
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
