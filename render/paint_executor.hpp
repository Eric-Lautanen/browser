#pragma once
#include <vector>
#include <unordered_map>
#include "paint.hpp"
#include "text_renderer.hpp"
#include "renderer.hpp"
#include "texture.hpp"
#include "../css/layout.hpp"
#include "../image/format.hpp"

namespace browser::render {

class PaintExecutor {
public:
    PaintExecutor(Renderer* r, TextRenderer* tr);
    void execute(const DisplayList& list);
    void set_offset(f32 x, f32 y);
    void set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>>& images);
private:
    Renderer* renderer_;
    TextRenderer* text_renderer_;
    f32 offset_x_ = 0, offset_y_ = 0;
    std::vector<css::Rect> clip_stack_;
    std::unordered_map<ImageId, std::unique_ptr<class Texture2D>> texture_cache_;
    const std::unordered_map<std::string, std::shared_ptr<image::Image>>* images_ = nullptr;
    class Texture2D* get_or_create_texture(const image::Image& img);
};


} // namespace browser::render
