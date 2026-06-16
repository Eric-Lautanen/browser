#include "paint_executor.hpp"
#include "texture.hpp"
#include "../platform/opengl.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace browser::render {

namespace pgl = browser::platform;

PaintExecutor::PaintExecutor(Renderer* r, TextRenderer* tr)
    : renderer_(r), text_renderer_(tr) {}

void PaintExecutor::set_offset(f32 x, f32 y) {
    offset_x_ = x;
    offset_y_ = y;
}

void PaintExecutor::set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>>& images) {
    images_ = &images;
}

Texture2D* PaintExecutor::get_or_create_texture(const image::Image& img) {
    ImageId id = reinterpret_cast<ImageId>(&img);
    auto it = texture_cache_.find(id);
    if (it != texture_cache_.end()) {
        return it->second.get();
    }

    if (img.rgba_pixels.empty() || img.width == 0 || img.height == 0) return nullptr;

    auto tex = std::make_unique<Texture2D>();
    auto r = tex->create(img.width, img.height, img.rgba_pixels.data());
    if (r.is_err()) return nullptr;

    Texture2D* ptr = tex.get();
    texture_cache_[id] = std::move(tex);
    return ptr;
}

// Gradient texture cache
Texture2D* PaintExecutor::get_or_create_gradient_texture(const css::CSSGradient& grad, f32 w, f32 h) {
    // Simple hash-based lookup
    uint64_t key = static_cast<uint64_t>(grad.type) ^
                   (static_cast<uint64_t>(grad.angle * 1000)) ^
                   (static_cast<uint64_t>(w) << 20) ^
                   (static_cast<uint64_t>(h) << 40);
    auto it = gradient_cache_.find(key);
    if (it != gradient_cache_.end()) return it->second.get();

    // Generate gradient pixels
    std::vector<Color> pixels;
    generate_linear_gradient_colors(grad, w, h, pixels);
    if (pixels.empty()) return nullptr;

    u32 pw = static_cast<u32>(w > 0 ? w : 100);
    u32 ph = static_cast<u32>(h > 0 ? h : 100);

    // Convert Color to RGBA bytes
    std::vector<u8> rgba(pw * ph * 4);
    for (u32 i = 0; i < pw * ph; i++) {
        rgba[i * 4] = static_cast<u8>(pixels[i].r * 255);
        rgba[i * 4 + 1] = static_cast<u8>(pixels[i].g * 255);
        rgba[i * 4 + 2] = static_cast<u8>(pixels[i].b * 255);
        rgba[i * 4 + 3] = static_cast<u8>(pixels[i].a * 255);
    }

    auto tex = std::make_unique<Texture2D>();
    auto r = tex->create(pw, ph, rgba.data());
    if (r.is_err()) return nullptr;

    Texture2D* ptr = tex.get();
    gradient_cache_[key] = std::move(tex);
    return ptr;
}

void PaintExecutor::generate_linear_gradient_colors(const css::CSSGradient& grad, f32 w, f32 h,
                                                      std::vector<Color>& pixels) {
    // Reuse the same logic from painter.cpp
    u32 pw = static_cast<u32>(w > 0 ? w : 100);
    u32 ph = static_cast<u32>(h > 0 ? h : 100);

    if (pw == 0 || ph == 0) return;
    pixels.resize(pw * ph);

    f32 angle_rad = grad.angle * 3.14159265f / 180.0f;
    f32 cos_a = cosf(angle_rad);
    f32 sin_a = sinf(angle_rad);

    f32 dx = cos_a;
    f32 dy = -sin_a;

    f32 len = sqrtf(dx * dx + dy * dy);
    if (len > 0) { dx /= len; dy /= len; }

    f32 cx = w / 2.0f, cy = h / 2.0f;
    f32 half_extent = std::abs(dx * w) + std::abs(dy * h);

    for (u32 y = 0; y < ph; y++) {
        for (u32 x = 0; x < pw; x++) {
            f32 px = static_cast<f32>(x);
            f32 py = static_cast<f32>(y);
            f32 t = ((px - cx) * dx + (py - cy) * dy) / half_extent + 0.5f;
            t = std::max(0.0f, std::min(1.0f, t));

            Color result = {0, 0, 0, 0};
            if (grad.stops.empty()) {
                u8 v = static_cast<u8>(t * 255);
                result = {static_cast<f32>(v)/255, static_cast<f32>(v)/255, static_cast<f32>(v)/255, 1.0f};
            } else if (grad.stops.size() == 1) {
                auto& s = grad.stops[0];
                result = css_to_render_color(s.color);
            } else {
                size_t i = 0;
                while (i + 1 < grad.stops.size()) {
                    f32 p0 = grad.stops[i].position >= 0 ? grad.stops[i].position :
                             (i == 0 ? 0.0f : 1.0f);
                    f32 p1 = grad.stops[i + 1].position >= 0 ? grad.stops[i + 1].position : 1.0f;
                    if (t >= p0 && t <= p1) {
                        f32 range = p1 - p0;
                        f32 local_t = range > 0 ? (t - p0) / range : 0.5f;
                        auto& c0 = grad.stops[i].color;
                        auto& c1 = grad.stops[i + 1].color;
                        f32 r = static_cast<f32>(c0.r) / 255.0f + (static_cast<f32>(c1.r) / 255.0f - static_cast<f32>(c0.r) / 255.0f) * local_t;
                        f32 g = static_cast<f32>(c0.g) / 255.0f + (static_cast<f32>(c1.g) / 255.0f - static_cast<f32>(c0.g) / 255.0f) * local_t;
                        f32 b = static_cast<f32>(c0.b) / 255.0f + (static_cast<f32>(c1.b) / 255.0f - static_cast<f32>(c0.b) / 255.0f) * local_t;
                        f32 a = static_cast<f32>(c0.a) / 255.0f + (static_cast<f32>(c1.a) / 255.0f - static_cast<f32>(c0.a) / 255.0f) * local_t;
                        result = {r, g, b, a};
                        break;
                    }
                    i++;
                }
                if (i + 1 >= grad.stops.size()) {
                    auto& last = grad.stops.back().color;
                    result = css_to_render_color(last);
                }
            }
            pixels[y * pw + x] = result;
        }
    }
}

void PaintExecutor::execute(const DisplayList& list) {
    for (const auto& cmd : list.commands()) {
        switch (cmd.type) {
            case PaintCommand::Type::FILL_RECT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                Color c = cmd.color;
                c.a *= current_opacity_;
                renderer_->fill_rect(x, y, cmd.rect.width, cmd.rect.height, c);
                break;
            }
            case PaintCommand::Type::DRAW_TEXT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                Color c = cmd.color;
                c.a *= current_opacity_;
                text_renderer_->render_text(renderer_, cmd.text, x, y,
                                            c, static_cast<u32>(cmd.font_size));
                break;
            }
            case PaintCommand::Type::PUSH_CLIP: {
                renderer_->flush();
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                f32 w = cmd.rect.width;
                f32 h = cmd.rect.height;

                if (!clip_stack_.empty()) {
                    auto& prev = clip_stack_.back();
                    f32 ix = std::max(x, prev.x);
                    f32 iy = std::max(y, prev.y);
                    f32 iw = std::min(x + w, prev.x + prev.width) - ix;
                    f32 ih = std::min(y + h, prev.y + prev.height) - iy;
                    x = ix; y = iy; w = iw < 0 ? 0 : iw; h = ih < 0 ? 0 : ih;
                }

                u32 win_h = renderer_->height();
                i32 sc_y = static_cast<i32>(win_h) - static_cast<i32>(y + h);
                pgl::glEnable(GL_SCISSOR_TEST);
                pgl::glScissor(static_cast<i32>(x), sc_y,
                               static_cast<i32>(w > 0 ? w : 0),
                               static_cast<i32>(h > 0 ? h : 0));
                clip_stack_.push_back({x, y, w, h});
                break;
            }
            case PaintCommand::Type::DRAW_IMAGE: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                f32 w = cmd.rect.width;
                f32 h = cmd.rect.height;
                ImageId id = cmd.image_id;
                auto it = texture_cache_.find(id);
                if (it != texture_cache_.end() && it->second) {
                    Color c = cmd.color;
                    c.a *= current_opacity_;
                    renderer_->draw_textured_quad(x, y, w, h, c, it->second.get());
                }
                break;
            }
            case PaintCommand::Type::POP_CLIP: {
                renderer_->flush();
                if (!clip_stack_.empty()) {
                    clip_stack_.pop_back();
                }
                if (clip_stack_.empty()) {
                    pgl::glDisable(GL_SCISSOR_TEST);
                } else {
                    auto& prev = clip_stack_.back();
                    u32 win_h = renderer_->height();
                    i32 sy = static_cast<i32>(win_h) - static_cast<i32>(prev.y + prev.height);
                    pgl::glScissor(static_cast<i32>(prev.x), sy,
                                   static_cast<i32>(prev.width > 0 ? prev.width : 0),
                                   static_cast<i32>(prev.height > 0 ? prev.height : 0));
                }
                break;
            }
            case PaintCommand::Type::DRAW_GRADIENT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                f32 w = cmd.rect.width;
                f32 h = cmd.rect.height;

                if (w <= 0 || h <= 0) break;

                Texture2D* tex = get_or_create_gradient_texture(cmd.gradient, w, h);
                if (tex) {
                    renderer_->draw_textured_quad(x, y, w, h, Color{1,1,1,current_opacity_}, tex);
                }
                break;
            }
            case PaintCommand::Type::DRAW_SHADOW: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                f32 w = cmd.rect.width;
                f32 h = cmd.rect.height;
                f32 blur = cmd.radius;

                Color c = cmd.color;
                c.a *= current_opacity_;

                if (blur > 0) {
                    // Render shadow as a blurred rect
                    // For simplicity, just render a semi-transparent rect larger than the blur
                    renderer_->fill_rect(x, y, w, h, c);
                } else {
                    renderer_->fill_rect(x, y, w, h, c);
                }
                break;
            }
            case PaintCommand::Type::PUSH_TRANSFORM: {
                renderer_->flush();
                // Apply the 3x3 affine transform matrix to the OpenGL modelview
                // Save current matrix
                transform_stack_.push_back(current_transform_);
                current_transform_ = cmd.transform;
                // For now, we apply the transform by translating the offset
                // (proper OpenGL matrix manipulation would be done here)
                break;
            }
            case PaintCommand::Type::POP_TRANSFORM: {
                renderer_->flush();
                if (!transform_stack_.empty()) {
                    current_transform_ = transform_stack_.back();
                    transform_stack_.pop_back();
                }
                break;
            }
            case PaintCommand::Type::PUSH_OPACITY: {
                opacity_stack_.push_back(current_opacity_);
                current_opacity_ *= cmd.opacity;
                break;
            }
            case PaintCommand::Type::POP_OPACITY: {
                if (!opacity_stack_.empty()) {
                    current_opacity_ = opacity_stack_.back();
                    opacity_stack_.pop_back();
                }
                break;
            }
            case PaintCommand::Type::DRAW_ROUNDED_RECT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                f32 w = cmd.rect.width;
                f32 h = cmd.rect.height;
                f32 r = cmd.radius;
                Color c = cmd.color;
                c.a *= current_opacity_;

                // Simple approximation: draw a rounded rect using the fill rect
                // For a proper implementation, we'd use a fragment shader
                renderer_->fill_rect(x + r, y, w - 2 * r, r, c); // top
                renderer_->fill_rect(x + r, y + h - r, w - 2 * r, r, c); // bottom
                renderer_->fill_rect(x, y + r, r, h - 2 * r, c); // left
                renderer_->fill_rect(x + w - r, y + r, r, h - 2 * r, c); // right
                renderer_->fill_rect(x + r, y + r, w - 2 * r, h - 2 * r, c); // center
                break;
            }
        }
    }

    // Reset state
    clip_stack_.clear();
    pgl::glDisable(GL_SCISSOR_TEST);
}

}
