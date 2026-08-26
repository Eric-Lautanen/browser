#include "executor.hpp"

#include "../../platform/opengl.hpp"
#include "../offscreen_target.hpp"
#include "../texture.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace browser::render {

    namespace pgl = browser::platform;

    PaintExecutor::PaintExecutor(Renderer *r, TextRenderer *tr) : renderer_(r), text_renderer_(tr) {}

    void PaintExecutor::set_base_clip(f32 x, f32 y, f32 w, f32 h) {
        has_base_clip_ = true;
        base_clip_ = {x, y, w, h};
        apply_clip_rect(base_clip_);
    }

    void PaintExecutor::apply_clip_rect(const css::Rect &r) {
        u32 win_h = renderer_->height();
        i32 sc_y = static_cast<i32>(win_h) - static_cast<i32>(r.y + r.height);
        pgl::glEnable(GL_SCISSOR_TEST);
        pgl::glScissor(static_cast<i32>(r.x),
                       sc_y,
                       static_cast<GLsizei>(r.width > 0 ? r.width : 0),
                       static_cast<GLsizei>(r.height > 0 ? r.height : 0));
    }

    bool PaintExecutor::is_identity(const css::Mat3x3 &m) const {
        return m.m[0][0] == 1 && m.m[0][1] == 0 && m.m[0][2] == 0 && m.m[1][0] == 0 && m.m[1][1] == 1 &&
               m.m[1][2] == 0 && m.m[2][0] == 0 && m.m[2][1] == 0 && m.m[2][2] == 1;
    }

    void PaintExecutor::transform_rect(f32 &x, f32 &y, f32 &w, f32 &h) const {
        f32 corners[4][2] = {{x, y}, {x + w, y}, {x, y + h}, {x + w, y + h}};
        f32 min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
        for (int i = 0; i < 4; i++) {
            f32 px = corners[i][0], py = corners[i][1];
            f32 tx = current_transform_.m[0][0] * px + current_transform_.m[0][1] * py + current_transform_.m[0][2];
            f32 ty = current_transform_.m[1][0] * px + current_transform_.m[1][1] * py + current_transform_.m[1][2];
            if (tx < min_x)
                min_x = tx;
            if (ty < min_y)
                min_y = ty;
            if (tx > max_x)
                max_x = tx;
            if (ty > max_y)
                max_y = ty;
        }
        x = min_x;
        y = min_y;
        w = max_x - min_x;
        h = max_y - min_y;
    }

    void PaintExecutor::set_offset(f32 x, f32 y) {
        offset_x_ = x;
        offset_y_ = y;
    }

    void PaintExecutor::set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>> &images) {
        images_ = &images;
    }

    Texture2D *PaintExecutor::get_or_create_texture(const image::Image &img) {
        ImageId id = reinterpret_cast<ImageId>(&img);
        auto it = texture_cache_.find(id);
        if (it != texture_cache_.end()) {
            return it->second.get();
        }

        if (img.rgba_pixels.empty() || img.width == 0 || img.height == 0)
            return nullptr;

        auto tex = std::make_unique<Texture2D>();
        auto r = tex->create(img.width, img.height, img.rgba_pixels.data(), true);
        if (r.is_err())
            return nullptr;

        Texture2D *ptr = tex.get();
        texture_cache_[id] = std::move(tex);
        return ptr;
    }

    Texture2D *PaintExecutor::get_or_create_gradient_texture(const css::CSSGradient &grad, f32 w, f32 h) {
        uint64_t key = static_cast<uint64_t>(grad.type) ^ (static_cast<uint64_t>(grad.angle * 1000)) ^
                       (static_cast<uint64_t>(w) << 20) ^ (static_cast<uint64_t>(h) << 40);
        // Hash all stop colors and positions into the key
        for (const auto &stop : grad.stops) {
            uint64_t stop_key = (static_cast<uint64_t>(stop.color.r) << 48) ^
                                (static_cast<uint64_t>(stop.color.g) << 32) ^
                                (static_cast<uint64_t>(stop.color.b) << 16) ^ static_cast<uint64_t>(stop.color.a);
            key ^= stop_key ^ (static_cast<uint64_t>(static_cast<i64>(stop.position * 10000.0f)));
            key = (key << 7) | (key >> 57);
        }
        auto it = gradient_cache_.find(key);
        if (it != gradient_cache_.end())
            return it->second.get();

        std::vector<Color> pixels;
        generate_linear_gradient_colors(grad, w, h, pixels);
        if (pixels.empty())
            return nullptr;

        u32 pw = static_cast<u32>(w > 0 ? w : 100);
        u32 ph = static_cast<u32>(h > 0 ? h : 100);

        std::vector<u8> rgba(pw * ph * 4);
        for (u32 i = 0; i < pw * ph; i++) {
            rgba[i * 4] = static_cast<u8>(pixels[i].r * 255);
            rgba[i * 4 + 1] = static_cast<u8>(pixels[i].g * 255);
            rgba[i * 4 + 2] = static_cast<u8>(pixels[i].b * 255);
            rgba[i * 4 + 3] = static_cast<u8>(pixels[i].a * 255);
        }

        auto tex = std::make_unique<Texture2D>();
        auto r = tex->create(pw, ph, rgba.data(), true);
        if (r.is_err())
            return nullptr;

        Texture2D *ptr = tex.get();
        gradient_cache_[key] = std::move(tex);
        return ptr;
    }

    void PaintExecutor::invalidate_page_caches() {
        // Image and canvas pointers died with the old document; gradient
        // textures are value-keyed but cheap to rebuild lazily.
        texture_cache_.clear();
        canvas_cache_.clear();
        gradient_cache_.clear();
    }

    const Texture2D *PaintExecutor::cached_canvas_texture(void *canvas_id) const {
        auto it = canvas_cache_.find(canvas_id);
        return it == canvas_cache_.end() ? nullptr : it->second.tex.get();
    }

    void PaintExecutor::execute(const DisplayList &list) {
        for (const auto &cmd : list.commands()) {
            switch (cmd.type) {
                case PaintCommand::Type::FILL_RECT: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;
                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }
                    Color c = cmd.color;
                    c.a *= current_opacity_;
                    renderer_->fill_rect(x, y, w, h, c);
                    break;
                }
                case PaintCommand::Type::DRAW_TEXT: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    Color c = cmd.color;
                    c.a *= current_opacity_;
                    text_renderer_->render_text(
                        renderer_, cmd.text, x, y, c, static_cast<u32>(cmd.font_size), cmd.font_flags);
                    break;
                }
                case PaintCommand::Type::PUSH_CLIP: {
                    renderer_->flush();
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;

                    // Intersect with base clip
                    if (has_base_clip_) {
                        f32 ix = std::max(x, base_clip_.x);
                        f32 iy = std::max(y, base_clip_.y);
                        f32 iw = std::min(x + w, base_clip_.x + base_clip_.width) - ix;
                        f32 ih = std::min(y + h, base_clip_.y + base_clip_.height) - iy;
                        x = ix;
                        y = iy;
                        w = iw < 0 ? 0 : iw;
                        h = ih < 0 ? 0 : ih;
                    }

                    // Intersect with existing clip stack top
                    if (!clip_stack_.empty()) {
                        auto &prev = clip_stack_.back();
                        f32 ix = std::max(x, prev.x);
                        f32 iy = std::max(y, prev.y);
                        f32 iw = std::min(x + w, prev.x + prev.width) - ix;
                        f32 ih = std::min(y + h, prev.y + prev.height) - iy;
                        x = ix;
                        y = iy;
                        w = iw < 0 ? 0 : iw;
                        h = ih < 0 ? 0 : ih;
                    }

                    apply_clip_rect({x, y, w, h});
                    clip_stack_.push_back({x, y, w, h});
                    break;
                }
                case PaintCommand::Type::DRAW_IMAGE: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;
                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }
                    ImageId id = cmd.image_id;
                    bool nearest = (cmd.image_flags & 1) != 0;
                    auto it = texture_cache_.find(id);
                    if (it != texture_cache_.end() && it->second) {
                        Color c = cmd.color;
                        c.a *= current_opacity_;
                        renderer_->draw_textured_quad(x, y, w, h, c, it->second.get(), nearest);
                    } else if (images_ && id) {
                        auto img = reinterpret_cast<const image::Image *>(id);
                        auto tex = get_or_create_texture(*img);
                        if (tex) {
                            Color c = cmd.color;
                            c.a *= current_opacity_;
                            renderer_->draw_textured_quad(x, y, w, h, c, tex, nearest);
                        }
                    }
                    break;
                }
                case PaintCommand::Type::POP_CLIP: {
                    renderer_->flush();
                    if (!clip_stack_.empty()) {
                        clip_stack_.pop_back();
                    }
                    if (clip_stack_.empty()) {
                        if (has_base_clip_) {
                            apply_clip_rect(base_clip_);
                        } else {
                            pgl::glDisable(GL_SCISSOR_TEST);
                        }
                    } else {
                        apply_clip_rect(clip_stack_.back());
                    }
                    break;
                }
                case PaintCommand::Type::DRAW_GRADIENT: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;

                    if (w <= 0 || h <= 0)
                        break;

                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }

                    Texture2D *tex = get_or_create_gradient_texture(cmd.gradient, w, h);
                    if (tex) {
                        renderer_->draw_textured_quad(x, y, w, h, Color{1, 1, 1, current_opacity_}, tex);
                    }
                    break;
                }
                case PaintCommand::Type::DRAW_SHADOW: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;
                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }
                    Color c = cmd.color;
                    c.a *= current_opacity_;
                    renderer_->fill_rect(x, y, w, h, c);
                    break;
                }
                case PaintCommand::Type::PUSH_TRANSFORM: {
                    renderer_->flush();
                    transform_stack_.push_back(current_transform_);
                    current_transform_ = cmd.transform;
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
                case PaintCommand::Type::DRAW_CANVAS: {
                    if (cmd.canvas_pixels.empty() || cmd.canvas_data_w == 0 || cmd.canvas_data_h == 0)
                        break;
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;
                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }

                    // R-P1: cache keyed by the canvas object identity, reused
                    // across frames; upload only when the canvas version moved
                    // on or the buffer was resized. The pixel-buffer address is
                    // never a cache key (it is not stable).
                    auto cache_it = canvas_cache_.find(cmd.canvas_id);
                    if (cache_it == canvas_cache_.end()) {
                        auto tex = std::make_unique<Texture2D>();
                        auto r = tex->create(cmd.canvas_data_w, cmd.canvas_data_h, cmd.canvas_pixels.data(), true);
                        if (r.is_err())
                            break;
                        CanvasTextureEntry entry;
                        entry.tex = std::move(tex);
                        entry.version = cmd.canvas_version;
                        entry.width = cmd.canvas_data_w;
                        entry.height = cmd.canvas_data_h;
                        cache_it = canvas_cache_.emplace(cmd.canvas_id, std::move(entry)).first;
                    } else if (cache_it->second.version != cmd.canvas_version ||
                               cache_it->second.width != cmd.canvas_data_w ||
                               cache_it->second.height != cmd.canvas_data_h) {
                        if (cache_it->second.width != cmd.canvas_data_w ||
                            cache_it->second.height != cmd.canvas_data_h) {
                            // Resized: full re-create (dimensions changed).
                            auto r = cache_it->second.tex->create(
                                cmd.canvas_data_w, cmd.canvas_data_h, cmd.canvas_pixels.data(), true);
                            if (r.is_err())
                                break;
                        } else {
                            cache_it->second.tex->update_sub(
                                0, 0, cmd.canvas_data_w, cmd.canvas_data_h, cmd.canvas_pixels.data(), true);
                        }
                        cache_it->second.version = cmd.canvas_version;
                        cache_it->second.width = cmd.canvas_data_w;
                        cache_it->second.height = cmd.canvas_data_h;
                    }
                    Color c = cmd.color;
                    c.a *= current_opacity_;
                    renderer_->draw_textured_quad(x, y, w, h, c, cache_it->second.tex.get());
                    break;
                }
                case PaintCommand::Type::DRAW_ROUNDED_RECT: {
                    f32 x = cmd.rect.x + offset_x_;
                    f32 y = cmd.rect.y + offset_y_;
                    f32 w = cmd.rect.width;
                    f32 h = cmd.rect.height;
                    f32 r = cmd.radius;
                    if (!is_identity(current_transform_)) {
                        transform_rect(x, y, w, h);
                    }
                    Color c = cmd.color;
                    c.a *= current_opacity_;

                    f32 inner_x1 = x + r, inner_y1 = y + r;
                    f32 inner_x2 = x + w - r, inner_y2 = y + h - r;
                    if (inner_x2 < inner_x1)
                        inner_x2 = inner_x1;
                    if (inner_y2 < inner_y1)
                        inner_y2 = inner_y1;
                    f32 inner_w = inner_x2 - inner_x1;
                    f32 inner_h = inner_y2 - inner_y1;

                    renderer_->fill_rect(inner_x1, y, inner_w, r, c);
                    renderer_->fill_rect(inner_x1, y + h - r, inner_w, r, c);
                    renderer_->fill_rect(x, inner_y1, r, inner_h, c);
                    renderer_->fill_rect(x + w - r, inner_y1, r, inner_h, c);
                    renderer_->fill_rect(inner_x1, inner_y1, inner_w, inner_h, c);

                    if (r > 0) {
                        i32 segments = static_cast<i32>(r * 0.5f);
                        if (segments < 4)
                            segments = 4;
                        if (segments > 16)
                            segments = 16;
                        auto arc_segment = [&](f32 cx, f32 cy, f32 start_angle, f32 end_angle) {
                            for (i32 i = 0; i < segments; i++) {
                                f32 a1 = start_angle + (end_angle - start_angle) * static_cast<f32>(i) / segments;
                                f32 a2 = start_angle + (end_angle - start_angle) * static_cast<f32>(i + 1) / segments;
                                f32 x1 = cx + r * cosf(a1);
                                f32 y1 = cy + r * sinf(a1);
                                f32 x2 = cx + r * cosf(a2);
                                f32 y2 = cy + r * sinf(a2);
                                f32 min_x = std::min({x1, x2, cx});
                                f32 min_y = std::min({y1, y2, cy});
                                f32 max_x = std::max({x1, x2, cx});
                                f32 max_y = std::max({y1, y2, cy});
                                renderer_->fill_rect(min_x, min_y, max_x - min_x, max_y - min_y, c);
                            }
                        };
                        arc_segment(x + r, y + r, 3.14159f, 3.14159f * 1.5f);
                        arc_segment(x + w - r, y + r, 3.14159f * 1.5f, 3.14159f * 2.0f);
                        arc_segment(x + r, y + h - r, 3.14159f * 0.5f, 3.14159f);
                        arc_segment(x + w - r, y + h - r, 0.0f, 3.14159f * 0.5f);
                    }
                    break;
                }
                case PaintCommand::Type::PUSH_FILTER: {
                    renderer_->flush();

                    FilterStackEntry entry;
                    entry.filters = cmd.filters;

                    // Check if we need an FBO for blur or drop-shadow
                    bool needs_fbo = false;
                    f32 max_blur = 0;
                    bool has_drop_shadow = false;
                    f32 ds_off_x = 0, ds_off_y = 0;
                    css::Color ds_color = {0, 0, 0, 255};
                    for (const auto &f : cmd.filters) {
                        if (f.type == css::CSSFilterFunc::Type::BLUR) {
                            needs_fbo = true;
                            f32 r = f.length_param.value;
                            if (r > max_blur)
                                max_blur = r;
                        } else if (f.type == css::CSSFilterFunc::Type::DROP_SHADOW) {
                            needs_fbo = true;
                            has_drop_shadow = true;
                            f32 r = f.amount;
                            if (r > max_blur)
                                max_blur = r;
                            ds_off_x = f.length_param.value;
                            ds_off_y = f.length_param2.value;
                            ds_color = f.color_param;
                        }
                    }

                    // R-P2: clamp blur so the (separable) kernel stays sane.
                    if (max_blur > Renderer::kMaxBlurRadius)
                        max_blur = Renderer::kMaxBlurRadius;

                    // Only create FBO if there isn't already one active
                    bool has_active_fbo = false;
                    for (auto &se : filter_stack_) {
                        if (se.fbo_state.fbo_borrowed) {
                            has_active_fbo = true;
                            break;
                        }
                    }

                    if (needs_fbo && !has_active_fbo) {
                        f32 pad = std::ceil(max_blur) * 2.0f;
                        f32 fb_w = cmd.rect.width + pad;
                        f32 fb_h = cmd.rect.height + pad;
                        if (fb_w < 1)
                            fb_w = 1;
                        if (fb_h < 1)
                            fb_h = 1;
                        u32 fbo_w = static_cast<u32>(fb_w);
                        u32 fbo_h = static_cast<u32>(fb_h);

                        // R-P3: take a pooled target that fits; allocate only
                        // when none does. Targets live in the pool for the
                        // executor's lifetime and are reused every frame.
                        OffscreenTarget *fbo = nullptr;
                        for (auto &t : fbo_pool_) {
                            if (t->width() >= fbo_w && t->height() >= fbo_h) {
                                fbo = t.get();
                                break;
                            }
                        }
                        if (!fbo) {
                            auto fresh = std::make_unique<OffscreenTarget>();
                            if (fresh->create(fbo_w, fbo_h).is_ok()) {
                                fbo = fresh.get();
                                fbo_pool_.push_back(std::move(fresh));
                            }
                        }

                        if (fbo) {
                            entry.fbo_state.fbo_borrowed = fbo;
                            entry.fbo_state.element_rect = cmd.rect;
                            entry.fbo_state.saved_offset_x = offset_x_;
                            entry.fbo_state.saved_offset_y = offset_y_;
                            entry.fbo_state.blur_radius = max_blur;
                            entry.fbo_state.has_drop_shadow = has_drop_shadow;
                            entry.fbo_state.ds_offset_x = ds_off_x;
                            entry.fbo_state.ds_offset_y = ds_off_y;
                            entry.fbo_state.ds_color = ds_color;

                            fbo->bind();
                            pgl::glViewport(0, 0, (GLsizei)fbo->width(), (GLsizei)fbo->height());
                            pgl::glClearColor(0, 0, 0, 0);
                            pgl::glClear(GL_COLOR_BUFFER_BIT);
                            // Disable scissor while rendering to FBO; clips would use wrong viewport height
                            pgl::glDisable(GL_SCISSOR_TEST);

                            offset_x_ = pad / 2.0f - cmd.rect.x;
                            offset_y_ = pad / 2.0f - cmd.rect.y;
                        }
                    }

                    filter_stack_.push_back(std::move(entry));
                    renderer_->set_filter_uniforms(cmd.filters);
                    break;
                }
                case PaintCommand::Type::POP_FILTER: {
                    renderer_->flush();

                    if (!filter_stack_.empty()) {
                        auto &entry = filter_stack_.back();

                        if (entry.fbo_state.fbo_borrowed) {
                            OffscreenTarget *fbo = entry.fbo_state.fbo_borrowed;

                            // Unbind FBO, restore main framebuffer
                            fbo->unbind();
                            pgl::glViewport(0, 0, (GLsizei)renderer_->width(), (GLsizei)renderer_->height());

                            // Restore offset
                            offset_x_ = entry.fbo_state.saved_offset_x;
                            offset_y_ = entry.fbo_state.saved_offset_y;
                            // R-G1: draw_blurred_texture restores scissor itself;
                            // the executor's clip state is untouched.

                            // Composite FBO texture to main framebuffer
                            f32 ex = entry.fbo_state.element_rect.x + offset_x_;
                            f32 ey = entry.fbo_state.element_rect.y + offset_y_;
                            f32 ew = entry.fbo_state.element_rect.width;
                            f32 eh = entry.fbo_state.element_rect.height;
                            f32 pad = std::ceil(entry.fbo_state.blur_radius) * 2.0f;
                            u32 tex_id = fbo->texture_id();
                            u32 src_w = fbo->width();
                            u32 src_h = fbo->height();

                            if (entry.fbo_state.has_drop_shadow) {
                                // Shadow color tint
                                Color shadow_tint = {static_cast<f32>(entry.fbo_state.ds_color.r) / 255.0f,
                                                     static_cast<f32>(entry.fbo_state.ds_color.g) / 255.0f,
                                                     static_cast<f32>(entry.fbo_state.ds_color.b) / 255.0f,
                                                     static_cast<f32>(entry.fbo_state.ds_color.a) / 255.0f};
                                // Draw shadow: blurred version at offset position, tinted
                                f32 sx = ex + entry.fbo_state.ds_offset_x - pad / 2.0f;
                                f32 sy = ey + entry.fbo_state.ds_offset_y - pad / 2.0f;
                                renderer_->draw_blurred_texture(
                                    sx,
                                    sy,
                                    ew + pad,
                                    eh + pad,
                                    tex_id,
                                    src_w,
                                    src_h,
                                    entry.fbo_state.blur_radius > 0 ? entry.fbo_state.blur_radius : 0.0f,
                                    shadow_tint);
                                // Draw original unblurred on top (no tint = white = identity)
                                renderer_->draw_blurred_texture(ex - pad / 2.0f,
                                                                ey - pad / 2.0f,
                                                                ew + pad,
                                                                eh + pad,
                                                                tex_id,
                                                                src_w,
                                                                src_h,
                                                                0.0f,
                                                                Color::WHITE);
                            } else if (entry.fbo_state.blur_radius > 0.0f) {
                                renderer_->draw_blurred_texture(ex - pad / 2.0f,
                                                                ey - pad / 2.0f,
                                                                ew + pad,
                                                                eh + pad,
                                                                tex_id,
                                                                src_w,
                                                                src_h,
                                                                entry.fbo_state.blur_radius);
                            } else {
                                // FBO with no blur and no drop-shadow: just draw as-is
                                renderer_->draw_blurred_texture(
                                    ex - pad / 2.0f, ey - pad / 2.0f, ew + pad, eh + pad, tex_id, src_w, src_h, 0.0f);
                            }
                        }

                        filter_stack_.pop_back();
                    }

                    // Restore previous filter uniforms
                    if (filter_stack_.empty()) {
                        renderer_->clear_filter_uniforms();
                    } else {
                        renderer_->set_filter_uniforms(filter_stack_.back().filters);
                    }
                    break;
                }
            }
        }

        clip_stack_.clear();
        if (has_base_clip_) {
            apply_clip_rect(base_clip_);
        } else {
            pgl::glDisable(GL_SCISSOR_TEST);
        }
    }
}  // namespace browser::render
