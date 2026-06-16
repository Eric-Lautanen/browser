#include "paint_executor.hpp"
#include "texture.hpp"
#include "../platform/opengl.hpp"
#include <algorithm>

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

void PaintExecutor::execute(const DisplayList& list) {
    for (const auto& cmd : list.commands()) {
        switch (cmd.type) {
            case PaintCommand::Type::FILL_RECT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                renderer_->fill_rect(x, y, cmd.rect.width, cmd.rect.height, cmd.color);
                break;
            }
            case PaintCommand::Type::DRAW_TEXT: {
                f32 x = cmd.rect.x + offset_x_;
                f32 y = cmd.rect.y + offset_y_;
                text_renderer_->render_text(renderer_, cmd.text, x, y,
                                            cmd.color, static_cast<u32>(cmd.font_size));
                break;
            }
            case PaintCommand::Type::PUSH_CLIP: {
                // Flush any batched geometry before changing scissor state
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
                    renderer_->draw_textured_quad(x, y, w, h, cmd.color, it->second.get());
                }
                break;
            }
            case PaintCommand::Type::POP_CLIP: {
                // Flush any batched geometry before changing scissor state
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
        }
    }
    // Reset scissor state after execute to avoid leaking into subsequent renders
    clip_stack_.clear();
    pgl::glDisable(GL_SCISSOR_TEST);
}

} // namespace browser::render
