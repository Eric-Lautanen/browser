#include "gradient.hpp"
#include "commands.hpp"
#include <cmath>
#include <algorithm>

namespace browser::render {

void generate_linear_gradient_colors(const css::CSSGradient& grad, f32 w, f32 h,
                                      std::vector<Color>& pixels) {
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

}
