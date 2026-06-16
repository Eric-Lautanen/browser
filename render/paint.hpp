#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include "../css/layout.hpp"
#include "renderer.hpp"

namespace browser::render {

using ImageId = std::uintptr_t;

struct PaintCommand {
    enum class Type { FILL_RECT, DRAW_TEXT, PUSH_CLIP, POP_CLIP, DRAW_IMAGE };
    Type type;
    css::Rect rect;
    Color color;
    std::string text;
    f32 font_size = 16;
    ImageId image_id = 0;
};

class DisplayList {
public:
    DisplayList() = default;
    DisplayList(DisplayList&&) = default;
    DisplayList& operator=(DisplayList&&) = default;
    void push(const PaintCommand& cmd);
    void clear();
    const std::vector<PaintCommand>& commands() const;
private:
    std::vector<PaintCommand> commands_;
};

inline Color css_to_render_color(const css::Color& c) {
    return {
        static_cast<f32>(c.r) / 255.0f,
        static_cast<f32>(c.g) / 255.0f,
        static_cast<f32>(c.b) / 255.0f,
        static_cast<f32>(c.a) / 255.0f
    };
}

} // namespace browser::render
