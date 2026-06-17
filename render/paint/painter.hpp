#pragma once
#include <memory>
#include <unordered_map>
#include "commands.hpp"
#include "../text_renderer.hpp"
#include "../../css/layout.hpp"
#include "../../image/format.hpp"
#include "../../async/task.hpp"
#include "../../async/executor.hpp"

namespace browser::render {

class Painter {
public:
    explicit Painter(TextRenderer* tr);
    void set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>>& images);
    async::task<std::shared_ptr<DisplayList>> paint_async(css::LayoutNode* root);
private:
    TextRenderer* text_renderer_;
    const std::unordered_map<std::string, std::shared_ptr<image::Image>>* images_ = nullptr;
    void paint_node(DisplayList& list, css::LayoutNode* node, f32 offset_x, f32 offset_y) const;
    void paint_background(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_border(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_text(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_image(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_shadow(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    void paint_outline(DisplayList& list, css::LayoutNode* node, f32 ox, f32 oy) const;
    Color resolve_color(const css::ComputedStyle& style, const std::string& prop, const Color& fallback) const;
    Color resolve_color_fallback(const css::ComputedStyle& style, const std::vector<std::string>& props, const Color& fallback) const;
    f32 resolve_font_size(const css::ComputedStyle& style) const;
};

}
