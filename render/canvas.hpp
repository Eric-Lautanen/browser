#pragma once
#include "../tests/utility.hpp"
#include "font/font.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

namespace browser::render {

extern std::unordered_map<const void*, std::shared_ptr<class Canvas2D>> g_canvas_registry;

class Canvas2D {
public:
    Canvas2D(u32 width, u32 height);
    ~Canvas2D() = default;
    Canvas2D(const Canvas2D&) = delete;
    Canvas2D& operator=(const Canvas2D&) = delete;

    u32 width() const { return width_; }
    u32 height() const { return height_; }
    const u8* pixels() const { return pixels_.data(); }

    void resize(u32 w, u32 h);

    void fill_rect(f32 x, f32 y, f32 w, f32 h);
    void stroke_rect(f32 x, f32 y, f32 w, f32 h);
    void clear_rect(f32 x, f32 y, f32 w, f32 h);

    void fill_text(const std::string& text, f32 x, f32 y);
    void stroke_text(const std::string& text, f32 x, f32 y);

    void begin_path();
    void move_to(f32 x, f32 y);
    void line_to(f32 x, f32 y);
    void arc(f32 x, f32 y, f32 r, f32 start_angle, f32 end_angle, bool ccw = false);
    void bezier_curve_to(f32 cp1x, f32 cp1y, f32 cp2x, f32 cp2y, f32 x, f32 y);
    void quadratic_curve_to(f32 cpx, f32 cpy, f32 x, f32 y);
    void close_path();
    void fill();
    void stroke();
    void clip();

    void save();
    void restore();

    void translate(f32 x, f32 y);
    void rotate(f32 angle);
    void scale(f32 x, f32 y);
    void transform(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f);
    void set_transform(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f);

    void set_fill_style(f32 r, f32 g, f32 b, f32 a);
    void set_stroke_style(f32 r, f32 g, f32 b, f32 a);
    void set_global_alpha(f32 a) { global_alpha_ = a; }
    void set_line_width(f32 w) { line_width_ = w < 0 ? 0 : w; }
    void set_font(const std::string& f);
    void set_text_align(const std::string& a) { text_align_ = a; }
    void set_text_baseline(const std::string& b) { text_baseline_ = b; }

    f32 global_alpha() const { return global_alpha_; }
    f32 line_width() const { return line_width_; }
    std::string font() const { return font_; }
    std::string text_align() const { return text_align_; }
    std::string text_baseline() const { return text_baseline_; }

    void get_image_data(u32 sx, u32 sy, u32 sw, u32 sh, std::vector<u8>& out) const;
    void put_image_data(const std::vector<u8>& data, u32 dx, u32 dy, u32 dw, u32 dh);
    std::string to_data_url() const;

    u32 version() const { return version_; }

    void set_font_manager(FontManager* fm) { font_manager_ = fm; }

private:
    u32 width_;
    u32 height_;
    std::vector<u8> pixels_;
    u32 version_ = 0;

    f32 fill_r_ = 0, fill_g_ = 0, fill_b_ = 0, fill_a_ = 1.0f;
    f32 stroke_r_ = 0, stroke_g_ = 0, stroke_b_ = 0, stroke_a_ = 1.0f;
    f32 global_alpha_ = 1.0f;
    f32 line_width_ = 1.0f;
    std::string font_ = "10px sans-serif";
    std::string text_align_ = "start";
    std::string text_baseline_ = "alphabetic";

    struct TransformT { f32 a, b, c, d, e, f; };

    struct State {
        f32 fill_r, fill_g, fill_b, fill_a;
        f32 stroke_r, stroke_g, stroke_b, stroke_a;
        f32 global_alpha, line_width;
        std::string font, text_align, text_baseline;
        TransformT transform;
    };
    std::vector<State> saved_states_;

    TransformT transform_{1,0,0,1,0,0};

    struct PathPt { f32 x, y; };
    std::vector<PathPt> current_path_;
    std::vector<std::vector<PathPt>> sub_paths_;
    f32 cx_ = 0, cy_ = 0;
    bool has_sub_path_ = false;

    FontManager* font_manager_ = nullptr;
    FontFace* resolve_font();

    void apply_transform(f32& x, f32& y) const;
    void set_pixel(u32 x, u32 y, u8 r, u8 g, u8 b, u8 a);
    void draw_line_low(f32 x0, f32 y0, f32 x1, f32 y1);
    void draw_line_high(f32 x0, f32 y0, f32 x1, f32 y1);
    void draw_line(f32 x0, f32 y0, f32 x1, f32 y1);
    void fill_scanline(f32 y, f32 x0, f32 x1);
    void add_bounded_arc(f32 cx, f32 cy, f32 r, f32 a0, f32 a1, bool ccw);
    void bezier_split(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, int depth);
    void rasterize_glyph(u32 codepoint, f32 x, f32 y, f32 size, bool fill);
};

// Called from browser initialization to hook canvas getContext onto DOM elements
void setup_canvas_bindings();

} // namespace browser::render
