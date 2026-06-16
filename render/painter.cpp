#include "painter.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace browser::render {

Painter::Painter(TextRenderer* tr)
    : text_renderer_(tr) {}

namespace {

PaintCommand make_cmd(PaintCommand::Type type, css::Rect rect, Color color,
                       const std::string& text = "", f32 font_size = 16,
                       ImageId image_id = 0,
                       const css::CSSGradient& gradient = {},
                       f32 radius = 0,
                       const css::Mat3x3& transform = {},
                       f32 opacity = 1.0f) {
    PaintCommand cmd;
    cmd.type = type;
    cmd.rect = rect;
    cmd.color = color;
    cmd.text = text;
    cmd.font_size = font_size;
    cmd.image_id = image_id;
    cmd.gradient = gradient;
    cmd.radius = radius;
    cmd.transform = transform;
    cmd.opacity = opacity;
    return cmd;
}

}

void Painter::set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>>& images) {
    images_ = &images;
}

async::task<std::shared_ptr<DisplayList>> Painter::paint_async(css::LayoutNode* root) {
    co_await async::thread_pool_executor{};
    auto list = std::make_shared<DisplayList>();
    if (root) paint_node(*list, root, 0, 0);
    co_return list;
}

void Painter::paint_node(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    if (!node) return;

    auto* opacity_val = node->style().get("opacity");
    f32 opacity = 1.0f;
    if (opacity_val && opacity_val->type == css::CSSValue::Type::NUMBER) {
        opacity = std::max(0.0f, std::min(1.0f, opacity_val->number));
    }
    node->opacity = opacity;

    bool needs_opacity_layer = opacity < 1.0f;
    if (needs_opacity_layer) {
        list.push(make_cmd(PaintCommand::Type::PUSH_OPACITY, {}, Color::TRANSPARENT, "", 0, 0, {}, 0, {}, opacity));
    }

    if (node->has_transform) {
        list.push(make_cmd(PaintCommand::Type::PUSH_TRANSFORM, {}, Color::TRANSPARENT, "", 0, 0, {}, 0, node->transform_matrix));
    }

    paint_background(list, node, ox, oy);
    paint_shadow(list, node, ox, oy);
    paint_border(list, node, ox, oy);
    paint_image(list, node, ox, oy);

    bool has_clip = false;
    auto* overflow = node->style().get("overflow");
    if (overflow && overflow->type == css::CSSValue::Type::KEYWORD) {
        has_clip = (overflow->keyword == "hidden" || overflow->keyword == "scroll" ||
                    overflow->keyword == "auto");
    }
    if (!has_clip) {
        auto* ox_val = node->style().get("overflow-x");
        if (ox_val && ox_val->type == css::CSSValue::Type::KEYWORD) {
            has_clip = (ox_val->keyword == "hidden" || ox_val->keyword == "scroll" ||
                        ox_val->keyword == "auto");
        }
    }
    if (!has_clip) {
        auto* oy_val = node->style().get("overflow-y");
        if (oy_val && oy_val->type == css::CSSValue::Type::KEYWORD) {
            has_clip = (oy_val->keyword == "hidden" || oy_val->keyword == "scroll" ||
                        oy_val->keyword == "auto");
        }
    }

    if (has_clip) {
        f32 px = ox - node->padding.left;
        f32 py = oy - node->padding.top;
        f32 pw = node->content.width + node->padding.left + node->padding.right;
        f32 ph = node->content.height + node->padding.top + node->padding.bottom;
        list.push(make_cmd(PaintCommand::Type::PUSH_CLIP, {px, py, pw, ph}, Color::TRANSPARENT));
    }

    if (node->is_text()) {
        paint_text(list, node, ox, oy);
    }

    // Paint children in z-index order: negative z → normal flow → positive z
    // First pass: children without z-index or z-index:auto (normal flow)
    // Second pass: children with positive z-index (sorted ascending)
    // Already painted before children: elements with negative z-index
    // For simplicity, we sort children by z-index value
    // Sort children by z-index for correct paint order
    // Negative z-index paints first, then normal flow, then positive z-index
    // Use a manual approach to avoid const correctness issues
    std::vector<css::LayoutNode*> sorted_children;
    for (auto& child : node->children) {
        sorted_children.push_back(child.get());
    }
    // Simple insertion sort by z-index
    for (size_t i = 1; i < sorted_children.size(); i++) {
        css::LayoutNode* key = sorted_children[i];
        f32 key_z = css::LayoutEngine::get_z_index(key->style());
        i32 j = static_cast<i32>(i) - 1;
        while (j >= 0 && css::LayoutEngine::get_z_index(sorted_children[j]->style()) > key_z) {
            sorted_children[static_cast<size_t>(j + 1)] = sorted_children[static_cast<size_t>(j)];
            j--;
        }
        sorted_children[static_cast<size_t>(j + 1)] = key;
    }

    for (auto* child : sorted_children) {
        paint_node(list, child,
                   ox + child->content.x + child->scroll_offset_x,
                   oy + child->content.y + child->scroll_offset_y);
    }

    if (has_clip) {
        list.push(make_cmd(PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT));
    }

    if (node->has_transform) {
        list.push(make_cmd(PaintCommand::Type::POP_TRANSFORM, {}, Color::TRANSPARENT));
    }

    if (needs_opacity_layer) {
        list.push(make_cmd(PaintCommand::Type::POP_OPACITY, {}, Color::TRANSPARENT));
    }
}

void Painter::paint_background(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    // Check for gradient background
    auto* bg_img = node->style().get("background-image");
    if (bg_img && bg_img->type == css::CSSValue::Type::GRADIENT) {
        f32 bx = ox - node->padding.left;
        f32 by = oy - node->padding.top;
        f32 bw = node->content.width + node->padding.left + node->padding.right
                 + node->border.left + node->border.right;
        f32 bh = node->content.height + node->padding.top + node->padding.bottom
                 + node->border.top + node->border.bottom;

        list.push(make_cmd(PaintCommand::Type::DRAW_GRADIENT, {bx, by, bw, bh}, Color::TRANSPARENT, "", 0, 0, bg_img->gradient));
        return;
    }

    Color bg = resolve_color(node->style(), "background-color", Color::TRANSPARENT);
    if (bg.a == 0.0f) return;

    f32 bx = ox - node->padding.left;
    f32 by = oy - node->padding.top;
    f32 bw = node->content.width + node->padding.left + node->padding.right
             + node->border.left + node->border.right;
    f32 bh = node->content.height + node->padding.top + node->padding.bottom
             + node->border.top + node->border.bottom;

    auto* br = node->style().get("border-radius");
    f32 radius = 0;
    if (br && br->type == css::CSSValue::Type::LENGTH) {
        radius = br->length.value;
    }

    if (radius > 0) {
        list.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT, {bx, by, bw, bh}, bg, "", 0, 0, {}, radius));
    } else {
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, bg));
    }
}

void Painter::paint_shadow(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    // box-shadow
    auto* bs = node->style().get("box-shadow");
    if (bs && bs->type == css::CSSValue::Type::STRING && !bs->string_value.empty()) {
        std::string s = bs->string_value;
        char* end = nullptr;
        f32 off_x = std::strtof(s.c_str(), &end);
        if (end && *end != '\0') {
            while (*end == ' ') end++;
            f32 off_y = std::strtof(end, &end);
            if (end) {
                while (*end == ' ') end++;
                f32 blur = std::strtof(end, &end);
                if (blur == 0 && end) blur = 0;

                Color shadow_color = {0, 0, 0, 0.5f};
                std::string rest = end ? end : "";
                while (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
                if (!rest.empty()) {
                    auto css_c = css::Color::from_name(rest);
                    if (css_c.a != 0 || rest == "transparent") {
                        shadow_color = css_to_render_color(css_c);
                    }
                }

                f32 bx = ox - node->padding.left + off_x;
                f32 by = oy - node->padding.top + off_y;
                f32 bw = node->content.width + node->padding.left + node->padding.right
                         + node->border.left + node->border.right;
                f32 bh = node->content.height + node->padding.top + node->padding.bottom
                         + node->border.top + node->border.bottom;

                if (blur > 0) {
                    list.push(make_cmd(PaintCommand::Type::DRAW_SHADOW, {bx - blur, by - blur, bw + 2 * blur, bh + 2 * blur}, shadow_color, "", 0, 0, {}, blur));
                } else {
                    list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, shadow_color));
                }
            }
        }
    }
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
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, b.top}, border_color));
    }
    if (b.right > 0.0f) {
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - b.right, by, b.right, bh}, border_right));
    }
    if (b.bottom > 0.0f) {
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - b.bottom, bw, b.bottom}, border_bottom));
    }
    if (b.left > 0.0f) {
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, b.left, bh}, border_left));
    }
}

void Painter::paint_text(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const {
    Color text_color = resolve_color(node->style(), "color", Color::BLACK);
    f32 font_size = resolve_font_size(node->style());
    f32 descender_pad = std::ceil(font_size * 0.25f);

    auto* ts = node->style().get("text-shadow");
    if (ts && ts->type == css::CSSValue::Type::STRING && !ts->string_value.empty() &&
        ts->string_value != "none") {
        std::string s = ts->string_value;
        char* end = nullptr;
        f32 sx = std::strtof(s.c_str(), &end);
        if (end) {
            while (*end == ' ') end++;
            f32 sy = std::strtof(end, &end);
            if (end) {
                Color shadow_color = {0, 0, 0, 0.5f};
                list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                    {ox + sx, oy + sy, node->content.width, node->content.height + descender_pad},
                    shadow_color, node->text(), font_size));
            }
        }
    }

    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
        {ox, oy, node->content.width, node->content.height + descender_pad},
        text_color, node->text(), font_size));
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

    list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE, {bx, by, bw, bh}, Color::WHITE, "", 0, id));
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

}
