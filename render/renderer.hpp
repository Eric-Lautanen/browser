#pragma once
#include "shader_program.hpp"
#include "mesh.hpp"
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
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Result<void> initialize(u32 window_width, u32 window_height);
    void begin_frame();
    void end_frame();
    void fill_rect(f32 x, f32 y, f32 w, f32 h, const Color& color);
    void stroke_rect(f32 x, f32 y, f32 w, f32 h, const Color& color, f32 line_width = 1.0f);
    void draw_line(f32 x1, f32 y1, f32 x2, f32 y2, const Color& color, f32 line_width = 1.0f);
    void fill_rect_tex(f32 x, f32 y, f32 w, f32 h, const Color& color, class Texture2D* texture);
    void draw_textured_quad(f32 x, f32 y, f32 w, f32 h, const Color& color, class Texture2D* texture);
    void begin_textured(class Texture2D* texture);
    void add_tex_quad(f32 x, f32 y, f32 w, f32 h, const Color& color, f32 u0, f32 v0, f32 u1, f32 v1);
    void end_textured();
    void set_viewport(u32 width, u32 height);
    void flush();
    u32 width() const { return width_; }
    u32 height() const { return height_; }
    bool needs_redraw() const { return needs_redraw_; }
    void set_needs_redraw(bool v = true) { needs_redraw_ = v; }
    void draw_icon(Icon icon, f32 x, f32 y, f32 size, const Color& color);
    void draw_icon_centered(Icon icon, f32 bx, f32 by, f32 bw, f32 bh,
                            f32 icon_size, const Color& color);

private:
    std::unique_ptr<ShaderProgram> shader_;
    std::unique_ptr<Mesh2D> batch_mesh_;
    u32 width_ = 0, height_ = 0;
    bool initialized_ = false;
    u32 current_texture_id_ = 0;
    bool textured_mode_ = false;
    bool needs_redraw_ = true;
    std::unique_ptr<IconAtlas> icon_atlas_;
};

}
