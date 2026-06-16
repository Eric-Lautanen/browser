#pragma once
#include "paint.hpp"
#include "renderer.hpp"
#include "text_renderer.hpp"
#include "../css/layout.hpp"
#include <vector>

namespace browser::render {

class PaintExecutor {
public:
    PaintExecutor(Renderer* r, TextRenderer* tr);
    void execute(const DisplayList& list);
    void set_offset(f32 x, f32 y);
private:
    Renderer* renderer_;
    TextRenderer* text_renderer_;
    f32 offset_x_ = 0, offset_y_ = 0;
    std::vector<css::Rect> clip_stack_;
};

} // namespace browser::render
