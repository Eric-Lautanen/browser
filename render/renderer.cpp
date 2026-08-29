#include "renderer.hpp"

#include "icons.hpp"
#include "offscreen_target.hpp"
#include "shaders.hpp"
#include "texture.hpp"

namespace browser::render {

    namespace pgl = browser::platform;

    // R-P3: reuse-or-create a scratch FBO at least as large as requested.
    // Filtered elements churn per frame; reallocating an FBO + texture for
    // each one each frame hammers the driver's memory manager.
    OffscreenTarget *Renderer::obtain_blur_scratch(u32 w, u32 h) {
        OffscreenTarget *best = nullptr;
        size_t best_idx = SIZE_MAX;
        for (size_t i = 0; i < blur_scratch_pool_.size(); i++) {
            auto &t = blur_scratch_pool_[i];
            if (t->width() >= w && t->height() >= h) {
                if (!best || (t->width() * t->height()) < (best->width() * best->height())) {
                    best = t.get();
                    best_idx = i;
                }
            }
        }
        if (best) {
            // Round-robin: move to the back so concurrent-size requests don't
            // thrash one target.
            if (best_idx + 1 != blur_scratch_pool_.size()) {
                auto owned = std::move(blur_scratch_pool_[best_idx]);
                blur_scratch_pool_.erase(blur_scratch_pool_.begin() + static_cast<std::ptrdiff_t>(best_idx));
                blur_scratch_pool_.push_back(std::move(owned));
                return blur_scratch_pool_.back().get();
            }
            return blur_scratch_pool_.back().get();
        }
        auto t = std::make_unique<OffscreenTarget>();
        if (t->create(w, h).is_err())
            return nullptr;
        blur_scratch_pool_.push_back(std::move(t));
        return blur_scratch_pool_.back().get();
    }

    const Color Color::RED = {1.0f, 0.0f, 0.0f, 1.0f};
    const Color Color::GREEN = {0.0f, 1.0f, 0.0f, 1.0f};
    const Color Color::BLUE = {0.0f, 0.0f, 1.0f, 1.0f};
    const Color Color::WHITE = {1.0f, 1.0f, 1.0f, 1.0f};
    const Color Color::BLACK = {0.0f, 0.0f, 0.0f, 1.0f};
    const Color Color::CYAN = {0.0f, 1.0f, 1.0f, 1.0f};
    const Color Color::TRANSPARENT = {0.0f, 0.0f, 0.0f, 0.0f};

    Mat4 Mat4::ortho(f32 left, f32 right, f32 bottom, f32 top) {
        Mat4 m = {};
        m.data[0] = 2.0f / (right - left);
        m.data[5] = 2.0f / (top - bottom);
        m.data[10] = -1.0f;
        m.data[12] = -(right + left) / (right - left);
        m.data[13] = -(top + bottom) / (top - bottom);
        m.data[15] = 1.0f;
        return m;
    }

    Renderer::Renderer() = default;
    Renderer::~Renderer() = default;

    Result<void> Renderer::initialize(u32 window_width, u32 window_height) {
        shader_ = std::make_unique<ShaderProgram>();
        batch_mesh_ = std::make_unique<Mesh2D>();

        auto r = shader_->compile(BASIC_VERTEX_SHADER, BASIC_FRAGMENT_SHADER);
        if (r.is_err())
            return r;

        // Compile blur post-process shader
        blur_shader_ = std::make_unique<ShaderProgram>();
        auto r2 = blur_shader_->compile(BASIC_VERTEX_SHADER, BLUR_FRAGMENT_SHADER);
        if (r2.is_err())
            return r2;
        blur_uniform_texture_ = blur_shader_->get_uniform_location("uBlurTexture");
        blur_uniform_radius_ = blur_shader_->get_uniform_location("uBlurRadius");

        width_ = window_width;
        height_ = window_height;

        pgl::glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        pgl::glEnable(GL_BLEND);
        pgl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Initialize projection matrix to avoid uninitialized uniform
        set_viewport(window_width, window_height);

        initialized_ = true;

        icon_atlas_ = std::make_unique<IconAtlas>();
        auto icon_r = icon_atlas_->initialize();
        if (icon_r.is_err())
            return icon_r;

        return {};
    }

    void Renderer::flush() {
        if (batch_mesh_->vertex_count() == 0)
            return;
        batch_mesh_->upload();
        batch_mesh_->draw();
        batch_mesh_->clear();
    }

    void Renderer::begin_frame() {
        pgl::glClear(GL_COLOR_BUFFER_BIT);
        batch_mesh_->clear();
        current_texture_id_ = 0;
        textured_mode_ = false;
    }

    void Renderer::end_frame() {
        // R-G5: end_textured() is a no-op when already in color mode.
        if (textured_mode_ || shader_mode_ != ShaderMode::Color)
            end_textured();

        if (fps_overlay_) {
            // Semi-transparent dark background in top-right corner
            f32 overlay_w = 220.0f;
            f32 overlay_h = 80.0f;
            f32 ox = (f32)width_ - overlay_w - 8.0f;
            f32 oy = 8.0f;
            batch_mesh_->add_quad(ox, oy, overlay_w, overlay_h, 0.0f, 0.0f, 0.0f, 0.6f);
        }

        flush();
        textured_mode_ = false;
        current_texture_id_ = 0;
    }

    void Renderer::toggle_fps_overlay() {
        fps_overlay_ = !fps_overlay_;
    }

    void Renderer::set_fps_data(
        f32 current, f32 min, f32 max, f32 avg, f32 events, f32 layout, f32 paint, f32 composite, f32 gpu) {
        fps_current_ = current;
        fps_min_ = min;
        fps_max_ = max;
        fps_avg_ = avg;
        fps_events_ = events;
        fps_layout_ = layout;
        fps_paint_ = paint;
        fps_composite_ = composite;
        fps_gpu_ = gpu;
    }

    void Renderer::fill_rect(f32 x, f32 y, f32 w, f32 h, const Color &color) {
        if (textured_mode_)
            end_textured();
        batch_mesh_->add_quad(x, y, w, h, color.r, color.g, color.b, color.a);
    }

    void Renderer::stroke_rect(f32 x, f32 y, f32 w, f32 h, const Color &color, f32 line_width) {
        if (line_width <= 0.0f)
            return;
        if (textured_mode_)
            end_textured();
        f32 r = color.r, g = color.g, b = color.b, a = color.a;

        // Simple 4-edge stroke: draw 4 axis-aligned quads (one per edge).
        // Top
        batch_mesh_->add_quad(x, y, w, line_width, r, g, b, a);
        // Bottom
        batch_mesh_->add_quad(x, y + h - line_width, w, line_width, r, g, b, a);
        // Left
        batch_mesh_->add_quad(x, y, line_width, h, r, g, b, a);
        // Right
        batch_mesh_->add_quad(x + w - line_width, y, line_width, h, r, g, b, a);
    }

    void Renderer::draw_line(f32 x1, f32 y1, f32 x2, f32 y2, const Color &color, f32 line_width) {
        if (textured_mode_)
            end_textured();
        batch_mesh_->add_line(x1, y1, x2, y2, color.r, color.g, color.b, color.a, line_width);
    }

    void Renderer::fill_rect_tex(f32 x, f32 y, f32 w, f32 h, const Color &color, Texture2D *texture) {
        draw_textured_quad(x, y, w, h, color, texture);
    }

    void Renderer::draw_textured_quad(f32 x, f32 y, f32 w, f32 h, const Color &color, Texture2D *texture, bool nearest) {
        if (!texture) {
            fill_rect(x, y, w, h, color);
            return;
        }
        u32 tid = texture->id();
        if (!textured_mode_ || current_texture_id_ != tid) {
            flush();
            shader_->bind();
            const auto &u = shader_->uniforms();
            if (u.use_texture >= 0)
                pgl::glUniform1i(u.use_texture, 1);
            if (u.texture_is_rgba >= 0)
                pgl::glUniform1i(u.texture_is_rgba, texture->is_rgba() ? 1 : 0);
            if (u.use_sdf >= 0)
                pgl::glUniform1i(u.use_sdf, 0);
            // Track the sampler state, or the next end_textured() would treat
            // the program as already in color mode and never unbind - fills
            // would keep sampling this texture (R-G5 invariant).
            shader_mode_ = ShaderMode::Tex;
            texture->bind(0);
            current_texture_id_ = tid;
            textured_mode_ = true;
        }
        if (nearest) {
            pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        batch_mesh_->add_quad_tex(x, y, w, h, color.r, color.g, color.b, color.a, 0.0f, 0.0f, 1.0f, 1.0f);
        if (nearest) {
            pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
    }

    void Renderer::begin_textured(Texture2D *texture) {
        if (textured_mode_ && shader_mode_ == ShaderMode::Tex && current_texture_id_ == texture->id()) {
            // R-G5: identical textured batch in flight — no state to rewrite.
            return;
        }
        flush();
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (shader_mode_ != ShaderMode::Tex) {
            if (u.use_texture >= 0)
                pgl::glUniform1i(u.use_texture, 1);
            if (u.texture_is_rgba >= 0)
                pgl::glUniform1i(u.texture_is_rgba, texture->is_rgba() ? 1 : 0);
            if (u.use_sdf >= 0)
                pgl::glUniform1i(u.use_sdf, 0);
            shader_mode_ = ShaderMode::Tex;
        }
        texture->bind(0);
        current_texture_id_ = texture->id();
        textured_mode_ = true;
    }

    void Renderer::begin_textured_sdf(Texture2D *texture) {
        if (textured_mode_ && shader_mode_ == ShaderMode::SDF && current_texture_id_ == texture->id()) {
            return;  // R-G5
        }
        flush();
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (shader_mode_ != ShaderMode::SDF) {
            if (u.use_texture >= 0)
                pgl::glUniform1i(u.use_texture, 1);
            if (u.texture_is_rgba >= 0)
                pgl::glUniform1i(u.texture_is_rgba, 0);
            if (u.use_sdf >= 0)
                pgl::glUniform1i(u.use_sdf, 1);
            shader_mode_ = ShaderMode::SDF;
        }
        texture->bind(0);
        current_texture_id_ = texture->id();
        textured_mode_ = true;
    }

    void Renderer::add_tex_quad(f32 x, f32 y, f32 w, f32 h, const Color &color, f32 u0, f32 v0, f32 u1, f32 v1) {
        batch_mesh_->add_quad_tex(x, y, w, h, color.r, color.g, color.b, color.a, u0, v0, u1, v1);
    }

    void Renderer::add_tex_quad_skewed(f32 x, f32 y, f32 w, f32 h, f32 skew, const Color &color, f32 u0, f32 v0, f32 u1, f32 v1) {
        batch_mesh_->add_quad_tex_skewed(x, y, w, h, skew, color.r, color.g, color.b, color.a, u0, v0, u1, v1);
    }

    void Renderer::end_textured() {
        if (textured_mode_)
            flush();
        textured_mode_ = false;
        current_texture_id_ = 0;
        // R-G5: reset to non-textured only when the program is not already
        // in color mode — repeated begin/end pairs wrote these every frame.
        if (shader_mode_ == ShaderMode::Color)
            return;
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (u.use_texture >= 0)
            pgl::glUniform1i(u.use_texture, 0);
        if (u.texture_is_rgba >= 0)
            pgl::glUniform1i(u.texture_is_rgba, 0);
        if (u.use_sdf >= 0)
            pgl::glUniform1i(u.use_sdf, 0);
        shader_mode_ = ShaderMode::Color;
    }

    void Renderer::draw_icon(Icon icon, f32 x, f32 y, f32 size, const Color &color) {
        if (icon_atlas_)
            icon_atlas_->draw(this, icon, x, y, size, color);
    }

    void Renderer::draw_icon_centered(Icon icon, f32 bx, f32 by, f32 bw, f32 bh, f32 icon_size, const Color &color) {
        if (icon_atlas_)
            icon_atlas_->draw_centered(this, icon, bx, by, bw, bh, icon_size, color);
    }

    void Renderer::set_viewport(u32 width, u32 height) {
        width_ = width;
        height_ = height;
        pgl::glViewport(0, 0, (GLsizei)width, (GLsizei)height);

        // R-G5: only the projection depends on the viewport; sampler uniforms
        // are owned by the shader_mode_ cache (begin/end_textured).
        Mat4 proj = Mat4::ortho(0.0f, (f32)width, (f32)height, 0.0f);
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (u.projection >= 0)
            pgl::glUniformMatrix4fv(u.projection, 1, GL_FALSE, proj.data);
    }

    void Renderer::set_filter_uniforms(const std::vector<css::CSSFilterFunc> &filters) {
        shader_->bind();
        const auto &u = shader_->uniforms();
        bool active = !filters.empty();
        if (u.filter_active >= 0)
            pgl::glUniform1i(u.filter_active, active ? 1 : 0);
        if (!active)
            return;

        f32 brightness = 1.0f;
        f32 contrast = 1.0f;
        f32 grayscale = 0.0f;
        f32 invert = 0.0f;
        f32 sepia = 0.0f;
        f32 saturate = 1.0f;
        f32 hue_rotate = 0.0f;
        f32 opacity = 1.0f;

        for (const auto &ff : filters) {
            switch (ff.type) {
                case css::CSSFilterFunc::Type::BLUR:
                    break;
                case css::CSSFilterFunc::Type::BRIGHTNESS:
                    brightness = ff.amount;
                    break;
                case css::CSSFilterFunc::Type::CONTRAST:
                    contrast = ff.amount;
                    break;
                case css::CSSFilterFunc::Type::GRAYSCALE:
                    grayscale = ff.amount > 0 ? ff.amount : 1.0f;
                    break;
                case css::CSSFilterFunc::Type::INVERT:
                    invert = ff.amount > 0 ? ff.amount : 1.0f;
                    break;
                case css::CSSFilterFunc::Type::SEPIA:
                    sepia = ff.amount > 0 ? ff.amount : 1.0f;
                    break;
                case css::CSSFilterFunc::Type::SATURATE:
                    saturate = ff.amount;
                    break;
                case css::CSSFilterFunc::Type::HUE_ROTATE:
                    hue_rotate = ff.amount;
                    break;
                case css::CSSFilterFunc::Type::OPACITY:
                    opacity = ff.amount;
                    break;
                case css::CSSFilterFunc::Type::DROP_SHADOW:
                    break;
            }
        }

        if (u.filter_brightness >= 0)
            pgl::glUniform1f(u.filter_brightness, brightness);
        if (u.filter_contrast >= 0)
            pgl::glUniform1f(u.filter_contrast, contrast);
        if (u.filter_grayscale >= 0)
            pgl::glUniform1f(u.filter_grayscale, grayscale);
        if (u.filter_invert >= 0)
            pgl::glUniform1f(u.filter_invert, invert);
        if (u.filter_sepia >= 0)
            pgl::glUniform1f(u.filter_sepia, sepia);
        if (u.filter_saturate >= 0)
            pgl::glUniform1f(u.filter_saturate, saturate);
        if (u.filter_hue_rotate >= 0)
            pgl::glUniform1f(u.filter_hue_rotate, hue_rotate);
        if (u.filter_opacity >= 0)
            pgl::glUniform1f(u.filter_opacity, opacity);
    }

    void Renderer::clear_filter_uniforms() {
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (u.filter_active >= 0)
            pgl::glUniform1i(u.filter_active, 0);
        if (u.filter_brightness >= 0)
            pgl::glUniform1f(u.filter_brightness, 1.0f);
        if (u.filter_contrast >= 0)
            pgl::glUniform1f(u.filter_contrast, 1.0f);
        if (u.filter_grayscale >= 0)
            pgl::glUniform1f(u.filter_grayscale, 0.0f);
        if (u.filter_invert >= 0)
            pgl::glUniform1f(u.filter_invert, 0.0f);
        if (u.filter_sepia >= 0)
            pgl::glUniform1f(u.filter_sepia, 0.0f);
        if (u.filter_saturate >= 0)
            pgl::glUniform1f(u.filter_saturate, 1.0f);
        if (u.filter_hue_rotate >= 0)
            pgl::glUniform1f(u.filter_hue_rotate, 0.0f);
        if (u.filter_opacity >= 0)
            pgl::glUniform1f(u.filter_opacity, 1.0f);
    }

    void Renderer::draw_blurred_texture(
        f32 x, f32 y, f32 w, f32 h, u32 texture_id, u32 src_w, u32 src_h, f32 blur_radius, const Color &tint) {
        if (texture_id == 0)
            return;

        // R-G1: save scissor state; this path temporarily disables clipping
        // and restores it exactly, so no caller needs to know.
        GLboolean scissor_was_enabled = pgl::glIsEnabled(GL_SCISSOR_TEST);
        GLint saved_scissor[4] = {0, 0, 0, 0};
        pgl::glGetIntegerv(GL_SCISSOR_BOX, saved_scissor);

        auto restore_scissor = [&]() {
            if (scissor_was_enabled) {
                pgl::glEnable(GL_SCISSOR_TEST);
                pgl::glScissor(saved_scissor[0], saved_scissor[1], saved_scissor[2], saved_scissor[3]);
            } else {
                pgl::glDisable(GL_SCISSOR_TEST);
            }
        };

        if (blur_radius <= 0.0f) {
            // No blur: draw directly with main shader as a textured quad with tint
            flush();
            pgl::glDisable(GL_SCISSOR_TEST);
            shader_->bind();
            const auto &u = shader_->uniforms();
            if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 1);
            if (u.texture_is_rgba >= 0) pgl::glUniform1i(u.texture_is_rgba, 1);
            if (u.use_sdf >= 0) pgl::glUniform1i(u.use_sdf, 0);
            pgl::glActiveTexture(GL_TEXTURE0);
            pgl::glBindTexture(GL_TEXTURE_2D, texture_id);

            batch_mesh_->clear();
            batch_mesh_->add_quad_tex(x, y, w, h, tint.r, tint.g, tint.b, tint.a, 0.0f, 0.0f, 1.0f, 1.0f);
            batch_mesh_->upload();
            batch_mesh_->draw();
            batch_mesh_->clear();

            // Reset to non-textured mode
            if (u.use_texture >= 0) pgl::glUniform1i(u.use_texture, 0);
            shader_mode_ = ShaderMode::Color;
            restore_scissor();
            return;
        }

        if (!blur_shader_) {
            restore_scissor();
            return;
        }
        flush();

        // R-P2: hard cap on blur radius â€” beyond ~64 px the two-pass kernel is
        // already enormous and unbounded radii risk GPU TDR.
        if (blur_radius > kMaxBlurRadius)
            blur_radius = kMaxBlurRadius;

        // R-P2: separable two-pass blur through a pooled scratch target.
        if (src_w == 0 || src_h == 0) {
            restore_scissor();
            return;
        }
        OffscreenTarget *scratch = obtain_blur_scratch(src_w, src_h);
        if (!scratch) {
            restore_scissor();
            return;
        }

        Mat4 proj = Mat4::ortho(0.0f, (f32)width_, (f32)height_, 0.0f);
        i32 blur_proj = blur_shader_->get_uniform_location("uProjection");
        i32 blur_dir = blur_shader_->get_uniform_location("uBlurDir");

        // Pass 1: horizontal blur into the scratch FBO at source resolution.
        flush();
        scratch->bind();
        pgl::glViewport(0, 0, (GLsizei)src_w, (GLsizei)src_h);
        blur_shader_->bind();
        Mat4 scratch_proj = Mat4::ortho(0.0f, (f32)src_w, (f32)src_h, 0.0f);
        if (blur_proj >= 0)
            pgl::glUniformMatrix4fv(blur_proj, 1, GL_FALSE, scratch_proj.data);
        if (blur_uniform_texture_ >= 0)
            pgl::glUniform1i(blur_uniform_texture_, 0);
        if (blur_uniform_radius_ >= 0)
            pgl::glUniform1f(blur_uniform_radius_, blur_radius);
        if (blur_dir >= 0)
            pgl::glUniform2f(blur_dir, 1.0f, 0.0f);
        pgl::glDisable(GL_SCISSOR_TEST);
        pgl::glActiveTexture(GL_TEXTURE0);
        pgl::glBindTexture(GL_TEXTURE_2D, texture_id);

        batch_mesh_->clear();
        batch_mesh_->add_quad_tex(0, 0, (f32)src_w, (f32)src_h, 1, 1, 1, 1, 0.0f, 0.0f, 1.0f, 1.0f);
        batch_mesh_->upload();
        batch_mesh_->draw();
        batch_mesh_->clear();

        // Pass 2: vertical blur from scratch onto the main framebuffer.
        scratch->unbind();
        pgl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        pgl::glViewport(0, 0, (GLsizei)width_, (GLsizei)height_);
        blur_shader_->bind();
        if (blur_proj >= 0)
            pgl::glUniformMatrix4fv(blur_proj, 1, GL_FALSE, proj.data);
        if (blur_dir >= 0)
            pgl::glUniform2f(blur_dir, 0.0f, 1.0f);
        pgl::glDisable(GL_SCISSOR_TEST);
        pgl::glActiveTexture(GL_TEXTURE0);
        pgl::glBindTexture(GL_TEXTURE_2D, scratch->texture_id());

        batch_mesh_->clear();
        batch_mesh_->add_quad_tex(x, y, w, h, tint.r, tint.g, tint.b, tint.a, 0.0f, 0.0f, 1.0f, 1.0f);
        batch_mesh_->upload();
        batch_mesh_->draw();
        batch_mesh_->clear();

        // Restore main shader state
        shader_->bind();
        const auto &u = shader_->uniforms();
        if (u.projection >= 0)
            pgl::glUniformMatrix4fv(u.projection, 1, GL_FALSE, proj.data);
        if (u.use_texture >= 0)
            pgl::glUniform1i(u.use_texture, 0);
        shader_mode_ = ShaderMode::Color;
        restore_scissor();
    }

}  // namespace browser::render
