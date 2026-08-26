#pragma once
#include "../../css/layout.hpp"
#include "../renderer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace browser::render {

    using ImageId = std::uintptr_t;

    struct PaintCommand {
        enum class Type {
            FILL_RECT,
            DRAW_TEXT,
            PUSH_CLIP,
            POP_CLIP,
            DRAW_IMAGE,
            DRAW_GRADIENT,
            DRAW_SHADOW,
            PUSH_TRANSFORM,
            POP_TRANSFORM,
            PUSH_OPACITY,
            POP_OPACITY,
            DRAW_ROUNDED_RECT,
            DRAW_CANVAS,
            PUSH_FILTER,
            POP_FILTER
        };
        Type type;
        css::Rect rect;
        Color color;
        std::string text;
        f32 font_size = 16;
        u8 font_flags = 0;  // bit 0=bold, bit 1=italic
        ImageId image_id = 0;
        css::CSSGradient gradient;
        f32 radius = 0;
        css::Mat3x3 transform;
        f32 opacity = 1.0f;
        u8 image_flags = 0;  // bit 0 = nearest-neighbor
        std::vector<u8> canvas_pixels;
        u32 canvas_data_w = 0;
        u32 canvas_data_h = 0;
        void *canvas_id = nullptr;  // owning Canvas2D* — stable cache key
        u32 canvas_version = 0;     // R-P1: upload only when this advances
        std::vector<css::CSSFilterFunc> filters;
    };

    class DisplayList {
    public:
        DisplayList() = default;
        DisplayList(DisplayList &&) = default;
        DisplayList &operator=(DisplayList &&) = default;
        void push(const PaintCommand &cmd) { commands_.push_back(cmd); }
        void clear() { commands_.clear(); }
        const std::vector<PaintCommand> &commands() const { return commands_; }

    private:
        std::vector<PaintCommand> commands_;
    };

    inline Color css_to_render_color(const css::Color &c) {
        return {static_cast<f32>(c.r) / 255.0f,
                static_cast<f32>(c.g) / 255.0f,
                static_cast<f32>(c.b) / 255.0f,
                static_cast<f32>(c.a) / 255.0f};
    }

}  // namespace browser::render
