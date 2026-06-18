#pragma once
#include "mesh.hpp"
#include "shader_program.hpp"

#include <memory>

// windows.h defines TRANSPARENT as a brush mode macro; undefine to avoid Color enum conflict
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif

namespace browser::render {

    enum class Icon : int;
    class IconAtlas;

    struct Color {
        f32 r, g, b, a;
        static const Color RED, GREEN, BLUE, WHITE, BLACK, CYAN, TRANSPARENT;
    };

    struct Mat4 {
        f32 data[16];
        static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top);
    };

    class Renderer {
    public:
        Renderer();
        ~Renderer();
        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;

        Result<void> initialize(u32 window_width, u32 window_height);
        void begin_frame();
        void end_frame();
        void fill_rect(f32 x, f32 y, f32 w, f32 h, const Color &color);
        void stroke_rect(f32 x, f32 y, f32 w, f32 h, const Color &color, f32 line_width = 1.0f);
        void draw_line(f32 x1, f32 y1, f32 x2, f32 y2, const Color &color, f32 line_width = 1.0f);
        void fill_rect_tex(f32 x, f32 y, f32 w, f32 h, const Color &color, class Texture2D *texture);
        void draw_textured_quad(f32 x, f32 y, f32 w, f32 h, const Color &color, class Texture2D *texture);
        void begin_textured(class Texture2D *texture);
    void add_tex_quad(f32 x, f32 y, f32 w, f32 h, const Color &color, f32 u0, f32 v0, f32 u1, f32 v1);
    void add_tex_quad_skewed(f32 x, f32 y, f32 w, f32 h, f32 skew, const Color &color, f32 u0, f32 v0, f32 u1, f32 v1);
    void end_textured();
        void set_viewport(u32 width, u32 height);
        void flush();
        u32 width() const { return width_; }
        u32 height() const { return height_; }
        bool needs_redraw() const { return needs_redraw_; }
        void set_needs_redraw(bool v = true) { needs_redraw_ = v; }
        void draw_icon(Icon icon, f32 x, f32 y, f32 size, const Color &color);
        void draw_icon_centered(Icon icon, f32 bx, f32 by, f32 bw, f32 bh, f32 icon_size, const Color &color);

        // FPS overlay
        void toggle_fps_overlay();
        void set_fps_data(
            f32 current, f32 min, f32 max, f32 avg, f32 events, f32 layout, f32 paint, f32 composite, f32 gpu);
        bool fps_overlay_visible() const { return fps_overlay_; }
        f32 fps_overlay_current() const { return fps_current_; }
        f32 fps_overlay_min() const { return fps_min_; }
        f32 fps_overlay_max() const { return fps_max_; }
        f32 fps_overlay_avg() const { return fps_avg_; }
        f32 fps_overlay_events() const { return fps_events_; }
        f32 fps_overlay_layout() const { return fps_layout_; }
        f32 fps_overlay_paint() const { return fps_paint_; }
        f32 fps_overlay_composite() const { return fps_composite_; }
        f32 fps_overlay_gpu() const { return fps_gpu_; }

    private:
        std::unique_ptr<ShaderProgram> shader_;
        std::unique_ptr<Mesh2D> batch_mesh_;
        u32 width_ = 0, height_ = 0;
        bool initialized_ = false;
        u32 current_texture_id_ = 0;
        bool textured_mode_ = false;
        bool needs_redraw_ = true;
        std::unique_ptr<IconAtlas> icon_atlas_;

        // FPS overlay state
        bool fps_overlay_ = false;
        f32 fps_current_ = 0;
        f32 fps_min_ = 0;
        f32 fps_max_ = 0;
        f32 fps_avg_ = 0;
        f32 fps_events_ = 0;
        f32 fps_layout_ = 0;
        f32 fps_paint_ = 0;
        f32 fps_composite_ = 0;
        f32 fps_gpu_ = 0;
    };

}  // namespace browser::render
