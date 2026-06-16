#pragma once
#include <memory>
#include "paint.hpp"
#include "text_renderer.hpp"
#include "../css/layout.hpp"
#include "../async/task.hpp"
#include "../async/executor.hpp"

namespace browser::render {

class Painter {
public:
    explicit Painter(TextRenderer* tr);
    async::task<std::shared_ptr<DisplayList>> paint(css::LayoutNode* root);
private:
    TextRenderer* text_renderer_;
    void paint_node(DisplayList& list, css::LayoutNode* node, f32 offset_x, f32 offset_y) const;
    void paint_background(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_border(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_text(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    Color resolve_color(const css::ComputedStyle& style, const std::string& prop, const Color& fallback) const;
    Color resolve_color_fallback(const css::ComputedStyle& style, const std::vector<std::string>& props, const Color& fallback) const;
    f32 resolve_font_size(const css::ComputedStyle& style) const;
};

} // namespace browser::render
