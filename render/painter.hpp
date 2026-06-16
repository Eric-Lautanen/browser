#pragma once
#include "paint.hpp"
#include "text_renderer.hpp"
#include "../css/layout.hpp"

namespace browser::render {

class Painter {
public:
    explicit Painter(TextRenderer* tr);
    void paint(css::LayoutNode* root);
    const DisplayList& display_list() const;
private:
    DisplayList list_;
    TextRenderer* text_renderer_;
    void paint_node(css::LayoutNode* node, f32 offset_x, f32 offset_y);
    void paint_background(css::LayoutNode* node, f32 ox, f32 oy);
    void paint_border(css::LayoutNode* node, f32 ox, f32 oy);
    void paint_text(css::LayoutNode* node, f32 ox, f32 oy);
    Color resolve_color(const css::ComputedStyle& style, const std::string& prop, const Color& fallback) const;
    Color resolve_color_fallback(const css::ComputedStyle& style, const std::vector<std::string>& props, const Color& fallback) const;
    f32 resolve_font_size(const css::ComputedStyle& style) const;
};

} // namespace browser::render
