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

    // State for transforms, opacity, etc.
    std::vector<css::Mat3x3> transform_stack_;
    css::Mat3x3 current_transform_;
    std::vector<f32> opacity_stack_;
    f32 current_opacity_ = 1.0f;
    std::unordered_map<uint64_t, std::unique_ptr<class Texture2D>> gradient_cache_;
    class Texture2D* get_or_create_gradient_texture(const css::CSSGradient& grad, f32 w, f32 h);
    void generate_linear_gradient_colors(const css::CSSGradient& grad, f32 w, f32 h,
                                          std::vector<Color>& pixels);

    // Transform helper
    void transform_rect(f32& x, f32& y, f32& w, f32& h) const;
    bool is_identity(const css::Mat3x3& m) const;
};

}
