#include "canvas.hpp"

#include "../html/utf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace browser::render {

    std::unordered_map<const void *, std::shared_ptr<Canvas2D>> g_canvas_registry;

    Canvas2D::Canvas2D(u32 width, u32 height) : width_(width), height_(height), pixels_(width * height * 4, 0) {}

    void Canvas2D::resize(u32 w, u32 h) {
        width_ = w;
        height_ = h;
        pixels_.assign(w * h * 4, 0);
        version_++;
    }

    void Canvas2D::set_pixel(u32 x, u32 y, u8 r, u8 g, u8 b, u8 a) {
        if (x >= width_ || y >= height_)
            return;
        u32 idx = (y * width_ + x) * 4;
        if (a == 255) {
            pixels_[idx] = r;
            pixels_[idx + 1] = g;
            pixels_[idx + 2] = b;
            pixels_[idx + 3] = a;
        } else if (a > 0) {
            f32 src_a = a / 255.0f;
            f32 dst_a = pixels_[idx + 3] / 255.0f;
            f32 out_a = src_a + dst_a * (1 - src_a);
            if (out_a > 0) {
                pixels_[idx] = static_cast<u8>((r * src_a + pixels_[idx] * dst_a * (1 - src_a)) / out_a);
                pixels_[idx + 1] = static_cast<u8>((g * src_a + pixels_[idx + 1] * dst_a * (1 - src_a)) / out_a);
                pixels_[idx + 2] = static_cast<u8>((b * src_a + pixels_[idx + 2] * dst_a * (1 - src_a)) / out_a);
                pixels_[idx + 3] = static_cast<u8>(out_a * 255);
            }
        }
    }

    void Canvas2D::apply_transform(f32 &x, f32 &y) const {
        f32 nx = transform_.a * x + transform_.c * y + transform_.e;
        f32 ny = transform_.b * x + transform_.d * y + transform_.f;
        x = nx;
        y = ny;
    }

    void Canvas2D::fill_rect(f32 x, f32 y, f32 w, f32 h) {
        version_++;
        f32 x1 = x + w, y1 = y + h;
        apply_transform(x, y);
        apply_transform(x1, y1);
        f32 min_x = std::min(x, x1), min_y = std::min(y, y1);
        f32 max_x = std::max(x, x1), max_y = std::max(y, y1);
        u32 sx = static_cast<u32>(std::max(0.0f, min_x));
        u32 sy = static_cast<u32>(std::max(0.0f, min_y));
        u32 ex = static_cast<u32>(std::min(static_cast<f32>(width_), max_x));
        u32 ey = static_cast<u32>(std::min(static_cast<f32>(height_), max_y));
        u8 r = static_cast<u8>(fill_r_ * 255);
        u8 g = static_cast<u8>(fill_g_ * 255);
        u8 b = static_cast<u8>(fill_b_ * 255);
        u8 a = static_cast<u8>(fill_a_ * global_alpha_ * 255);
        for (u32 py = sy; py < ey; py++)
            for (u32 px = sx; px < ex; px++) set_pixel(px, py, r, g, b, a);
    }

    void Canvas2D::stroke_rect(f32 x, f32 y, f32 w, f32 h) {
        version_++;
        f32 x1 = x + w, y1 = y + h;
        begin_path();
        move_to(x, y);
        line_to(x1, y);
        line_to(x1, y1);
        line_to(x, y1);
        close_path();
        stroke();
    }

    void Canvas2D::clear_rect(f32 x, f32 y, f32 w, f32 h) {
        version_++;
        f32 x1 = x + w, y1 = y + h;
        apply_transform(x, y);
        apply_transform(x1, y1);
        f32 min_x = std::min(x, x1), min_y = std::min(y, y1);
        f32 max_x = std::max(x, x1), max_y = std::max(y, y1);
        u32 sx = static_cast<u32>(std::max(0.0f, min_x));
        u32 sy = static_cast<u32>(std::max(0.0f, min_y));
        u32 ex = static_cast<u32>(std::min(static_cast<f32>(width_), max_x));
        u32 ey = static_cast<u32>(std::min(static_cast<f32>(height_), max_y));
        for (u32 py = sy; py < ey; py++)
            for (u32 px = sx; px < ex; px++) {
                u32 idx = (py * width_ + px) * 4;
                pixels_[idx] = pixels_[idx + 1] = pixels_[idx + 2] = pixels_[idx + 3] = 0;
            }
    }

    void Canvas2D::fill_text(const std::string &text, f32 x, f32 y) {
        version_++;
        f32 size = 16;
        auto sz_pos = font_.find('x');
        if (sz_pos != std::string::npos && sz_pos > 0) {
            std::string num_str = font_.substr(0, sz_pos);
            char *end = nullptr;
            f32 parsed = static_cast<f32>(std::strtod(num_str.c_str(), &end));
            if (end != num_str.c_str())
                size = parsed;
        }

        FontFace *face = resolve_font();
        f32 cur_x = x;
        const u8 *text_data = reinterpret_cast<const u8 *>(text.data());
        u32 text_offset = 0;
        u32 text_len = static_cast<u32>(text.size());
        while (text_offset < text_len) {
            auto dr = html::decode_utf8(text_data + text_offset, text_len - text_offset);
            if (dr.bytes_consumed == 0) {
                text_offset++;
                continue;
            }
            text_offset += dr.bytes_consumed;
            u32 cp = dr.codepoint;
            f32 gx = cur_x, gy = y;
            apply_transform(gx, gy);
            rasterize_glyph(cp, gx, gy, static_cast<u32>(size), true);
            if (face) {
                auto metrics = face->get_metrics(cp, static_cast<u32>(size));
                if (metrics.is_ok())
                    cur_x += metrics.unwrap().advance_x;
                else
                    cur_x += size * 0.6f;
            } else {
                cur_x += size * 0.6f;
            }
        }
    }

    void Canvas2D::stroke_text(const std::string &text, f32 x, f32 y) {
        version_++;
        f32 size = 16;
        auto sz_pos = font_.find('x');
        if (sz_pos != std::string::npos && sz_pos > 0) {
            std::string num_str = font_.substr(0, sz_pos);
            char *end = nullptr;
            f32 parsed = static_cast<f32>(std::strtod(num_str.c_str(), &end));
            if (end != num_str.c_str())
                size = parsed;
        }

        FontFace *face = resolve_font();
        f32 cur_x = x;
        const u8 *text_data = reinterpret_cast<const u8 *>(text.data());
        u32 text_offset = 0;
        u32 text_len = static_cast<u32>(text.size());
        while (text_offset < text_len) {
            auto dr = html::decode_utf8(text_data + text_offset, text_len - text_offset);
            if (dr.bytes_consumed == 0) {
                text_offset++;
                continue;
            }
            text_offset += dr.bytes_consumed;
            u32 cp = dr.codepoint;
            f32 gx = cur_x, gy = y;
            apply_transform(gx, gy);
            rasterize_glyph(cp, gx, gy, static_cast<u32>(size), false);
            if (face) {
                auto metrics = face->get_metrics(cp, static_cast<u32>(size));
                if (metrics.is_ok())
                    cur_x += metrics.unwrap().advance_x;
                else
                    cur_x += size * 0.6f;
            } else {
                cur_x += size * 0.6f;
            }
        }
    }

    void Canvas2D::begin_path() {
        current_path_.clear();
        sub_paths_.clear();
        has_sub_path_ = false;
    }

    void Canvas2D::move_to(f32 x, f32 y) {
        if (!current_path_.empty() && has_sub_path_)
            sub_paths_.push_back(current_path_);
        current_path_.clear();
        current_path_.push_back({x, y});
        cx_ = x;
        cy_ = y;
        has_sub_path_ = true;
    }

    void Canvas2D::line_to(f32 x, f32 y) {
        current_path_.push_back({x, y});
        cx_ = x;
        cy_ = y;
    }

    void Canvas2D::add_bounded_arc(f32 cx, f32 cy, f32 r, f32 a0, f32 a1, bool ccw) {
        if (r <= 0)
            return;
        f32 da = 0.05f;
        if (ccw) {
            if (a1 > a0)
                a1 -= 2 * 3.14159f;
        } else {
            if (a1 < a0)
                a1 += 2 * 3.14159f;
        }
        f32 steps = std::abs(a1 - a0) / da;
        if (steps < 1)
            steps = 1;
        if (steps > 200)
            steps = 200;
        da = (a1 - a0) / steps;
        bool first = !has_sub_path_;
        for (int i = 0; i <= static_cast<int>(steps); i++) {
            f32 angle = a0 + da * i;
            f32 px = cx + r * std::cos(angle);
            f32 py = cy + r * std::sin(angle);
            if (first) {
                current_path_.push_back({px, py});
                cx_ = px;
                cy_ = py;
                first = false;
                has_sub_path_ = true;
            } else {
                current_path_.push_back({px, py});
                cx_ = px;
                cy_ = py;
            }
        }
    }

    void Canvas2D::arc(f32 x, f32 y, f32 r, f32 start_angle, f32 end_angle, bool ccw) {
        add_bounded_arc(x, y, r, start_angle, end_angle, ccw);
    }

    void Canvas2D::bezier_split(f32 x0, f32 y0, f32 x1, f32 y1, f32 x2, f32 y2, f32 x3, f32 y3, int depth) {
        if (depth > 8) {
            current_path_.push_back({x3, y3});
            cx_ = x3;
            cy_ = y3;
            return;
        }
        f32 len = std::abs(x0 - x3) + std::abs(y0 - y3);
        f32 flat = std::abs(x1 - x0) + std::abs(y1 - y0) + std::abs(x2 - x3) + std::abs(y2 - y3);
        if (flat - len < 2.0f || depth >= 6) {
            current_path_.push_back({x3, y3});
            cx_ = x3;
            cy_ = y3;
            return;
        }
        f32 mx0 = (x0 + x1) * 0.5f, my0 = (y0 + y1) * 0.5f;
        f32 mx1 = (x1 + x2) * 0.5f, my1 = (y1 + y2) * 0.5f;
        f32 mx2 = (x2 + x3) * 0.5f, my2 = (y2 + y3) * 0.5f;
        f32 ax0 = (mx0 + mx1) * 0.5f, ay0 = (my0 + my1) * 0.5f;
        f32 ax1 = (mx1 + mx2) * 0.5f, ay1 = (my1 + my2) * 0.5f;
        f32 cx = (ax0 + ax1) * 0.5f, cy = (ay0 + ay1) * 0.5f;
        bezier_split(x0, y0, mx0, my0, ax0, ay0, cx, cy, depth + 1);
        bezier_split(cx, cy, ax1, ay1, mx2, my2, x3, y3, depth + 1);
    }

    void Canvas2D::bezier_curve_to(f32 cp1x, f32 cp1y, f32 cp2x, f32 cp2y, f32 x, f32 y) {
        f32 x0 = cx_, y0 = cy_;
        if (!current_path_.empty())
            current_path_.pop_back();
        bezier_split(x0, y0, cp1x, cp1y, cp2x, cp2y, x, y, 0);
    }

    void Canvas2D::quadratic_curve_to(f32 cpx, f32 cpy, f32 x, f32 y) {
        f32 x0 = cx_, y0 = cy_;
        f32 cp1x = x0 + (cpx - x0) * 2.0f / 3.0f;
        f32 cp1y = y0 + (cpy - y0) * 2.0f / 3.0f;
        f32 cp2x = x + (cpx - x) * 2.0f / 3.0f;
        f32 cp2y = y + (cpy - y) * 2.0f / 3.0f;
        bezier_curve_to(cp1x, cp1y, cp2x, cp2y, x, y);
    }

    void Canvas2D::close_path() {
        if (!current_path_.empty()) {
            current_path_.push_back(current_path_.front());
            cx_ = current_path_.front().x;
            cy_ = current_path_.front().y;
        }
    }

    void Canvas2D::draw_line_low(f32 x0, f32 y0, f32 x1, f32 y1) {
        f32 dx = x1 - x0, dy = y1 - y0;
        i32 yi = 1;
        if (dy < 0) {
            yi = -1;
            dy = -dy;
        }
        f32 D = 2 * dy - dx;
        f32 y = y0;
        for (f32 x = x0; x <= x1; x++) {
            set_pixel(static_cast<u32>(x),
                      static_cast<u32>(y),
                      static_cast<u8>(stroke_r_ * 255),
                      static_cast<u8>(stroke_g_ * 255),
                      static_cast<u8>(stroke_b_ * 255),
                      static_cast<u8>(stroke_a_ * global_alpha_ * 255));
            if (D > 0) {
                y += yi;
                D -= 2 * dx;
            }
            D += 2 * dy;
        }
    }

    void Canvas2D::draw_line_high(f32 x0, f32 y0, f32 x1, f32 y1) {
        f32 dx = x1 - x0, dy = y1 - y0;
        i32 xi = 1;
        if (dx < 0) {
            xi = -1;
            dx = -dx;
        }
        f32 D = 2 * dx - dy;
        f32 x = x0;
        for (f32 y = y0; y <= y1; y++) {
            set_pixel(static_cast<u32>(x),
                      static_cast<u32>(y),
                      static_cast<u8>(stroke_r_ * 255),
                      static_cast<u8>(stroke_g_ * 255),
                      static_cast<u8>(stroke_b_ * 255),
                      static_cast<u8>(stroke_a_ * global_alpha_ * 255));
            if (D > 0) {
                x += xi;
                D -= 2 * dy;
            }
            D += 2 * dx;
        }
    }

    void Canvas2D::draw_line(f32 x0, f32 y0, f32 x1, f32 y1) {
        if (std::abs(y1 - y0) < std::abs(x1 - x0)) {
            if (x0 > x1)
                draw_line_low(x1, y1, x0, y0);
            else
                draw_line_low(x0, y0, x1, y1);
        } else {
            if (y0 > y1)
                draw_line_high(x1, y1, x0, y0);
            else
                draw_line_high(x0, y0, x1, y1);
        }
    }

    void Canvas2D::fill_scanline(f32 y, f32 x0, f32 x1) {
        if (x0 > x1)
            std::swap(x0, x1);
        u32 sy = static_cast<u32>(std::max(0.0f, y));
        if (sy >= height_)
            return;
        u32 sx = static_cast<u32>(std::max(0.0f, x0));
        u32 ex = static_cast<u32>(std::min(static_cast<f32>(width_ - 1), x1));
        u8 r = static_cast<u8>(fill_r_ * 255);
        u8 g = static_cast<u8>(fill_g_ * 255);
        u8 b = static_cast<u8>(fill_b_ * 255);
        u8 a = static_cast<u8>(fill_a_ * global_alpha_ * 255);
        for (u32 px = sx; px <= ex; px++) set_pixel(px, sy, r, g, b, a);
    }

    void Canvas2D::fill() {
        if (current_path_.empty() && sub_paths_.empty())
            return;
        version_++;
        std::vector<PathPt> full_path;
        for (auto &sp : sub_paths_)
            for (auto &pt : sp) full_path.push_back(pt);
        for (auto &pt : current_path_) full_path.push_back(pt);
        if (full_path.size() < 3)
            return;

        f32 min_y = full_path[0].y, max_y = full_path[0].y;
        for (auto &pt : full_path) {
            if (pt.y < min_y)
                min_y = pt.y;
            if (pt.y > max_y)
                max_y = pt.y;
        }

        for (f32 y = std::max(0.0f, min_y); y <= std::min(static_cast<f32>(height_ - 1), max_y); y += 1.0f) {
            std::vector<f32> intersections;
            size_t n = full_path.size();
            for (size_t i = 0; i < n; i++) {
                auto &p1 = full_path[i];
                auto &p2 = full_path[(i + 1) % n];
                if ((p1.y <= y && p2.y > y) || (p2.y <= y && p1.y > y)) {
                    f32 t = (y - p1.y) / (p2.y - p1.y);
                    f32 ix = p1.x + t * (p2.x - p1.x);
                    intersections.push_back(ix);
                }
            }
            std::sort(intersections.begin(), intersections.end());
            for (size_t i = 0; i + 1 < intersections.size(); i += 2)
                fill_scanline(y, intersections[i], intersections[i + 1]);
        }
    }

    void Canvas2D::stroke() {
        if (current_path_.empty() && sub_paths_.empty())
            return;
        version_++;
        for (auto &sp : sub_paths_) {
            for (size_t i = 0; i + 1 < sp.size(); i++) draw_line(sp[i].x, sp[i].y, sp[i + 1].x, sp[i + 1].y);
        }
        for (size_t i = 0; i + 1 < current_path_.size(); i++)
            draw_line(current_path_[i].x, current_path_[i].y, current_path_[i + 1].x, current_path_[i + 1].y);
    }

    void Canvas2D::clip() {
        // For simplicity, clip is a no-op in the software renderer.
        // Real clip would maintain a clipping region.
    }

    void Canvas2D::save() {
        State s;
        s.fill_r = fill_r_;
        s.fill_g = fill_g_;
        s.fill_b = fill_b_;
        s.fill_a = fill_a_;
        s.stroke_r = stroke_r_;
        s.stroke_g = stroke_g_;
        s.stroke_b = stroke_b_;
        s.stroke_a = stroke_a_;
        s.global_alpha = global_alpha_;
        s.line_width = line_width_;
        s.font = font_;
        s.text_align = text_align_;
        s.text_baseline = text_baseline_;
        s.transform = transform_;
        saved_states_.push_back(s);
    }

    void Canvas2D::restore() {
        if (saved_states_.empty())
            return;
        auto &s = saved_states_.back();
        fill_r_ = s.fill_r;
        fill_g_ = s.fill_g;
        fill_b_ = s.fill_b;
        fill_a_ = s.fill_a;
        stroke_r_ = s.stroke_r;
        stroke_g_ = s.stroke_g;
        stroke_b_ = s.stroke_b;
        stroke_a_ = s.stroke_a;
        global_alpha_ = s.global_alpha;
        line_width_ = s.line_width;
        font_ = s.font;
        text_align_ = s.text_align;
        text_baseline_ = s.text_baseline;
        transform_ = s.transform;
        saved_states_.pop_back();
    }

    void Canvas2D::translate(f32 x, f32 y) {
        transform_.e += transform_.a * x + transform_.c * y;
        transform_.f += transform_.b * x + transform_.d * y;
    }

    void Canvas2D::rotate(f32 angle) {
        f32 c = std::cos(angle), s = std::sin(angle);
        f32 a = transform_.a * c + transform_.c * s;
        f32 b = transform_.b * c + transform_.d * s;
        f32 c2 = transform_.c * c - transform_.a * s;
        f32 d = transform_.d * c - transform_.b * s;
        transform_.a = a;
        transform_.b = b;
        transform_.c = c2;
        transform_.d = d;
    }

    void Canvas2D::scale(f32 x, f32 y) {
        transform_.a *= x;
        transform_.b *= x;
        transform_.c *= y;
        transform_.d *= y;
    }

    void Canvas2D::transform(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) {
        f32 na = transform_.a * a + transform_.c * b;
        f32 nb = transform_.b * a + transform_.d * b;
        f32 nc = transform_.a * c + transform_.c * d;
        f32 nd = transform_.b * c + transform_.d * d;
        f32 ne = transform_.a * e + transform_.c * f + transform_.e;
        f32 nf = transform_.b * e + transform_.d * f + transform_.f;
        transform_.a = na;
        transform_.b = nb;
        transform_.c = nc;
        transform_.d = nd;
        transform_.e = ne;
        transform_.f = nf;
    }

    void Canvas2D::set_transform(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f) {
        transform_.a = a;
        transform_.b = b;
        transform_.c = c;
        transform_.d = d;
        transform_.e = e;
        transform_.f = f;
    }

    void Canvas2D::set_fill_style(f32 r, f32 g, f32 b, f32 a) {
        fill_r_ = r;
        fill_g_ = g;
        fill_b_ = b;
        fill_a_ = a;
    }

    void Canvas2D::set_stroke_style(f32 r, f32 g, f32 b, f32 a) {
        stroke_r_ = r;
        stroke_g_ = g;
        stroke_b_ = b;
        stroke_a_ = a;
    }

    void Canvas2D::set_font(const std::string &f) {
        font_ = f;
    }

    FontFace *Canvas2D::resolve_font() {
        if (!font_manager_)
            return nullptr;
        auto r = font_manager_->load_default_font();
        return r.is_ok() ? r.unwrap() : nullptr;
    }

    void Canvas2D::rasterize_glyph(u32 codepoint, f32 x, f32 y, f32 size, bool fill) {
        FontFace *face = resolve_font();
        if (!face)
            return;
        u16 gid = static_cast<u16>(face->glyph_index(codepoint));
        if (gid == 0)
            return;
        auto glyph = face->rasterize_glyph_by_gid(gid, static_cast<u32>(size));
        if (glyph.is_err())
            return;
        auto &gb = glyph.unwrap();
        i32 px = static_cast<i32>(x) + gb.bearing_x;
        i32 py = static_cast<i32>(y) - gb.bearing_y;
        u8 fr = static_cast<u8>(fill ? (fill_r_ * 255) : (stroke_r_ * 255));
        u8 fg = static_cast<u8>(fill ? (fill_g_ * 255) : (stroke_g_ * 255));
        u8 fb = static_cast<u8>(fill ? (fill_b_ * 255) : (stroke_b_ * 255));
        f32 alpha_scale = fill ? (fill_a_ * global_alpha_) : (stroke_a_ * global_alpha_);
        for (u32 row = 0; row < gb.height; row++) {
            for (u32 col = 0; col < gb.width; col++) {
                u8 glyph_alpha = gb.bitmap[row * gb.width + col];
                if (glyph_alpha == 0)
                    continue;
                u32 dx = static_cast<u32>(std::max(0, px + static_cast<i32>(col)));
                u32 dy = static_cast<u32>(std::max(0, py + static_cast<i32>(row)));
                if (dx >= width_ || dy >= height_)
                    continue;
                u8 final_a = static_cast<u8>(std::min(255.0f, glyph_alpha * alpha_scale));
                set_pixel(dx, dy, fr, fg, fb, final_a);
            }
        }
    }

    void Canvas2D::get_image_data(u32 sx, u32 sy, u32 sw, u32 sh, std::vector<u8> &out) const {
        out.resize(sw * sh * 4, 0);
        for (u32 row = 0; row < sh; row++) {
            for (u32 col = 0; col < sw; col++) {
                u32 src_x = sx + col;
                u32 src_y = sy + row;
                u32 dst_idx = (row * sw + col) * 4;
                if (src_x < width_ && src_y < height_) {
                    u32 src_idx = (src_y * width_ + src_x) * 4;
                    out[dst_idx] = pixels_[src_idx];
                    out[dst_idx + 1] = pixels_[src_idx + 1];
                    out[dst_idx + 2] = pixels_[src_idx + 2];
                    out[dst_idx + 3] = pixels_[src_idx + 3];
                }
            }
        }
    }

    void Canvas2D::put_image_data(const std::vector<u8> &data, u32 dx, u32 dy, u32 dw, u32 dh) {
        version_++;
        for (u32 row = 0; row < dh; row++) {
            for (u32 col = 0; col < dw; col++) {
                u32 dst_x = dx + col;
                u32 dst_y = dy + row;
                if (dst_x >= width_ || dst_y >= height_)
                    continue;
                u32 src_idx = (row * dw + col) * 4;
                u32 dst_idx = (dst_y * width_ + dst_x) * 4;
                pixels_[dst_idx] = data[src_idx];
                pixels_[dst_idx + 1] = data[src_idx + 1];
                pixels_[dst_idx + 2] = data[src_idx + 2];
                pixels_[dst_idx + 3] = data[src_idx + 3];
            }
        }
    }

    static const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string Canvas2D::to_data_url() const {
        std::string result = "data:image/png;base64,";

        auto write_u32 = [](std::vector<u8> &buf, u32 val) {
            buf.push_back(static_cast<u8>((val >> 24) & 0xFF));
            buf.push_back(static_cast<u8>((val >> 16) & 0xFF));
            buf.push_back(static_cast<u8>((val >> 8) & 0xFF));
            buf.push_back(static_cast<u8>(val & 0xFF));
        };

        auto write_crc = [&](std::vector<u8> &buf, const u8 *data, u32 len) {
            u32 crc = 0xFFFFFFFF;
            for (u32 i = 0; i < len; i++) {
                crc ^= data[i];
                for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
            }
            write_u32(buf, crc ^ 0xFFFFFFFF);
        };

        // Build raw image data with filter bytes (None filter = 0x00 per row)
        std::vector<u8> raw_data;
        raw_data.reserve(height_ * (width_ * 4 + 1));
        for (u32 y = 0; y < height_; y++) {
            raw_data.push_back(0x00);  // filter byte: None
            u32 row_start = y * width_ * 4;
            for (u32 x = 0; x < width_ * 4; x++)
                raw_data.push_back(pixels_[row_start + x]);
        }

        // Compute Adler-32 of raw data
        u32 a1 = 1, a2 = 0;
        for (u8 b : raw_data) {
            a1 = (a1 + b) % 65521;
            a2 = (a2 + a1) % 65521;
        }
        u32 adler = (a2 << 16) | a1;

        // Build DEFLATE stored blocks interleaved with data
        std::vector<u8> deflate_data;
        // zlib header
        deflate_data.push_back(0x78);
        deflate_data.push_back(0x01);
        // Stored blocks: each block is header (5 bytes) + data
        u32 raw_offset = 0;
        u32 remaining = static_cast<u32>(raw_data.size());
        while (remaining > 0) {
            u32 chunk = remaining > 65535 ? 65535 : remaining;
            bool is_final = (remaining - chunk == 0);
            deflate_data.push_back(is_final ? 0x01 : 0x00);
            deflate_data.push_back(static_cast<u8>(chunk & 0xFF));
            deflate_data.push_back(static_cast<u8>((chunk >> 8) & 0xFF));
            u16 nlen = static_cast<u16>(~chunk & 0xFFFF);
            deflate_data.push_back(static_cast<u8>(nlen & 0xFF));
            deflate_data.push_back(static_cast<u8>((nlen >> 8) & 0xFF));
            deflate_data.insert(deflate_data.end(),
                                raw_data.data() + raw_offset,
                                raw_data.data() + raw_offset + chunk);
            raw_offset += chunk;
            remaining -= chunk;
        }
        // Adler-32 checksum
        deflate_data.push_back(static_cast<u8>((adler >> 24) & 0xFF));
        deflate_data.push_back(static_cast<u8>((adler >> 16) & 0xFF));
        deflate_data.push_back(static_cast<u8>((adler >> 8) & 0xFF));
        deflate_data.push_back(static_cast<u8>(adler & 0xFF));

        // Build PNG file
        std::vector<u8> png;
        // PNG signature
        png.insert(png.end(), {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});

        // IHDR chunk
        u32 ihdr_start = static_cast<u32>(png.size());
        write_u32(png, 13);  // IHDR length
        const char ihdr_type[] = "IHDR";
        png.insert(png.end(), ihdr_type, ihdr_type + 4);
        write_u32(png, width_);
        write_u32(png, height_);
        png.push_back(8);  // bit depth
        png.push_back(6);  // color type: RGBA
        png.push_back(0);  // compression
        png.push_back(0);  // filter
        png.push_back(0);  // interlace
        write_crc(png, &png[ihdr_start + 4], png.size() - ihdr_start - 4);

        // IDAT chunk
        u32 idat_start = static_cast<u32>(png.size());
        write_u32(png, static_cast<u32>(deflate_data.size()));
        const char idat_type[] = "IDAT";
        png.insert(png.end(), idat_type, idat_type + 4);
        png.insert(png.end(), deflate_data.data(), deflate_data.data() + deflate_data.size());
        write_crc(png, &png[idat_start + 4], png.size() - idat_start - 4);

        // IEND chunk
        u32 iend_start = static_cast<u32>(png.size());
        write_u32(png, 0);
        const char iend_type[] = "IEND";
        png.insert(png.end(), iend_type, iend_type + 4);
        write_crc(png, &png[iend_start + 4], png.size() - iend_start - 4);

        // Base64 encode
        for (size_t i = 0; i < png.size(); i += 3) {
            u8 b0 = png[i];
            u8 b1 = (i + 1 < png.size()) ? png[i + 1] : 0;
            u8 b2 = (i + 2 < png.size()) ? png[i + 2] : 0;
            result += kBase64Chars[(b0 >> 2) & 0x3F];
            result += kBase64Chars[((b0 << 4) | (b1 >> 4)) & 0x3F];
            result += (i + 1 < png.size()) ? kBase64Chars[((b1 << 2) | (b2 >> 6)) & 0x3F] : '=';
            result += (i + 2 < png.size()) ? kBase64Chars[b2 & 0x3F] : '=';
        }

        return result;
    }

}  // namespace browser::render
