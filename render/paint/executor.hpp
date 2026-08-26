#pragma once
#include "../../css/layout.hpp"
#include "../../image/format.hpp"
#include "../renderer.hpp"
#include "../text_renderer.hpp"
#include "../texture.hpp"
#include "../offscreen_target.hpp"
#include "commands.hpp"
#include "gradient.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace browser::render {

    struct FilterFBOState {
        std::unique_ptr<OffscreenTarget> fbo;
        css::Rect element_rect;   // element rect in screen space (with offset applied)
        f32 saved_offset_x = 0;
        f32 saved_offset_y = 0;
        f32 blur_radius = 0;      // for blur filter
        bool has_drop_shadow = false;
        f32 ds_offset_x = 0;      // drop-shadow horizontal offset
        f32 ds_offset_y = 0;      // drop-shadow vertical offset
        css::Color ds_color = {0, 0, 0, 255}; // drop-shadow color
    };

    struct FilterStackEntry {
        std::vector<css::CSSFilterFunc> filters;
        FilterFBOState fbo_state;  // valid only if fbo_state.fbo != nullptr
    };

    class PaintExecutor {
    public:
        PaintExecutor(Renderer *r, TextRenderer *tr);
        void execute(const DisplayList &list);
        void set_offset(f32 x, f32 y);
        void set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>> &images);
        void set_base_clip(f32 x, f32 y, f32 w, f32 h);

        // BR-P1: the executor is persistent across frames so its GPU texture
        // caches survive. Caches keyed by pointers owned by the CURRENT page
        // (image buffers, canvases) must be dropped when that page dies —
        // recycled addresses must never resolve to a stale texture.
        void invalidate_page_caches();

        // Observability for tests/diagnostics: the live cached texture for a
        // canvas id, or nullptr when none (or no longer) cached.
        const class Texture2D *cached_canvas_texture(void *canvas_id) const;

    private:
        // R-P1: canvas textures are reused across frames via version-guarded
        // upload; the pixel buffer address is never used as a cache key.
        struct CanvasTextureEntry {
            std::unique_ptr<class Texture2D> tex;
            u32 version = 0;
            u32 width = 0;
            u32 height = 0;
        };

        Renderer *renderer_;
        TextRenderer *text_renderer_;
        f32 offset_x_ = 0, offset_y_ = 0;
        bool has_base_clip_ = false;
        css::Rect base_clip_;
        std::vector<css::Rect> clip_stack_;
        std::unordered_map<ImageId, std::unique_ptr<class Texture2D>> texture_cache_;
        const std::unordered_map<std::string, std::shared_ptr<image::Image>> *images_ = nullptr;
        class Texture2D *get_or_create_texture(const image::Image &img);

        std::vector<css::Mat3x3> transform_stack_;
        css::Mat3x3 current_transform_;
        std::vector<f32> opacity_stack_;
        f32 current_opacity_ = 1.0f;
        std::unordered_map<uint64_t, std::unique_ptr<class Texture2D>> gradient_cache_;
        std::unordered_map<void *, CanvasTextureEntry> canvas_cache_;
        class Texture2D *get_or_create_gradient_texture(const css::CSSGradient &grad, f32 w, f32 h);

        // Filter state stack
        std::vector<FilterStackEntry> filter_stack_;

        void apply_clip_rect(const css::Rect &r);
        void transform_rect(f32 &x, f32 &y, f32 &w, f32 &h) const;
        bool is_identity(const css::Mat3x3 &m) const;
    };

}  // namespace browser::render
