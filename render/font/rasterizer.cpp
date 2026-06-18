#include "font.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace browser::render {

using internal::GlyphOutline;
using internal::read_u16_be;
using internal::read_i16_be;

std::vector<u8> FontFace::rasterize_scanline(const GlyphOutline& outline,
                                               u32 pw, u32 ph,
                                               i32 offset_x, i32 offset_y) {
    std::vector<u8> bitmap(pw * ph, 0);
    if (outline.num_contours <= 0 || outline.points.empty()) return bitmap;

    struct SortedEdge { f32 x0, y0, x1, y1; i32 dir; };
    std::vector<SortedEdge> edges;
    size_t pt_idx = 0;
    for (i16 c = 0; c < outline.num_contours; c++) {
        u16 end = outline.contour_end_pts[(size_t)c];
        u16 count = (c == 0) ? end + 1 : end - outline.contour_end_pts[(size_t)c - 1];
        for (u16 i = 0; i < count; i++) {
            size_t cur = pt_idx + i;
            size_t next = pt_idx + (i + 1) % count;
            f32 x0 = outline.points[cur * 2];
            f32 y0 = outline.points[cur * 2 + 1];
            f32 x1 = outline.points[next * 2];
            f32 y1 = outline.points[next * 2 + 1];
            if (y0 != y1) {
                i32 dir = (y1 > y0) ? 1 : -1;
                if (y0 > y1) { std::swap(x0, x1); std::swap(y0, y1); }
                edges.push_back({x0, y0, x1, y1, dir});
            }
        }
        pt_idx += count;
    }

    if (edges.empty()) return bitmap;

    std::sort(edges.begin(), edges.end(), [](const SortedEdge& a, const SortedEdge& b) {
        return a.y0 < b.y0;
    });

    static const f32 subsample_offsets[] = {-0.375f, -0.125f, 0.125f, 0.375f};

    const f32 base_y = (f32)offset_y;

    struct Crossing { f32 x; i32 dir; };

    for (u32 py = 0; py < ph; py++) {
        f32 scan_y = base_y + (f32)py;

        std::vector<Crossing> row_crossings[4];
        for (i32 sy = 0; sy < 4; sy++) {
            f32 sample_y = scan_y + subsample_offsets[sy];
            for (const auto& e : edges) {
                if (sample_y < e.y0 || sample_y >= e.y1) continue;
                f32 t = (sample_y - e.y0) / (e.y1 - e.y0);
                row_crossings[sy].push_back({e.x0 + t * (e.x1 - e.x0), e.dir});
            }
            std::sort(row_crossings[sy].begin(), row_crossings[sy].end(),
                      [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
        }

        for (u32 px = 0; px < pw; px++) {
            u32 pixel_coverage = 0;
            f32 base_x = (f32)(offset_x + (i32)px);

            for (i32 sy = 0; sy < 4; sy++) {
                i32 cidx = 0;
                i32 winding = 0;
                const auto& crossings = row_crossings[sy];
                for (i32 sx = 0; sx < 4; sx++) {
                    f32 sample_x = base_x + subsample_offsets[sx];
                    while (cidx < (i32)crossings.size() && crossings[cidx].x <= sample_x) {
                        winding += crossings[cidx].dir;
                        cidx++;
                    }
                    if (winding != 0) pixel_coverage++;
                }
            }

            bitmap[py * pw + px] = (u8)((pixel_coverage * 255) / 16);
        }
    }

    return bitmap;
}

Result<GlyphBitmap> FontFace::rasterize_glyph(u32 codepoint, u32 pixel_size) {
    u16 gid = (u16)glyph_index(codepoint);
    if (gid == 0) {
        return Result<GlyphBitmap>(std::string("glyph not in font"));
    }

    // Try color bitmap (emoji) first
    if (has_color_bitmap(codepoint, pixel_size)) {
        auto cr = rasterize_color_bitmap(codepoint);
        if (cr.is_ok()) return cr;
        // Fall through to outline on failure
    }

    int bezier_steps = (std::max)(4, (int)(pixel_size) / 4);
    auto outline_result = read_outline(gid, bezier_steps);
    if (outline_result.is_err()) {
        return Result<GlyphBitmap>(outline_result.unwrap_err());
    }
    auto& outline = outline_result.unwrap();

    // Apply variable font gvar deltas if variation coords are set
    if (!variation_coords_.empty() && outline.num_contours > 0) {
        apply_gvar_deltas(gid, outline.points, outline.num_contours);
    }

    auto metrics_result = get_metrics_by_gid(gid, pixel_size);
    if (metrics_result.is_err()) {
        return Result<GlyphBitmap>(metrics_result.unwrap_err());
    }
    auto& metrics = metrics_result.unwrap();

    f32 scale = (f32)pixel_size / units_per_em_;

    f32 x_min_px = (f32)outline.x_min * scale;
    f32 y_min_px = (f32)outline.y_min * scale;
    f32 x_max_px = (f32)outline.x_max * scale;
    f32 y_max_px = (f32)outline.y_max * scale;

    i32 pw = std::max(1, (i32)std::ceil(x_max_px - x_min_px) + 1);
    i32 ph = std::max(1, (i32)std::ceil(y_max_px - y_min_px) + 1);
    if (pw > 1024)
        pw = 1024;
    if (ph > 1024)
        ph = 1024;

    GlyphOutline scaled_outline;
    scaled_outline.num_contours = outline.num_contours;
    scaled_outline.contour_end_pts = outline.contour_end_pts;
    scaled_outline.points.resize(outline.points.size());
    for (size_t i = 0; i < outline.points.size(); i += 2) {
        f32 x = (outline.points[i] - (f32)outline.x_min) * scale;
        f32 y = ((f32)outline.y_max - outline.points[i + 1]) * scale;
        scaled_outline.points[i] = x;
        scaled_outline.points[i + 1] = y;
    }

    std::vector<u8> bitmap = rasterize_scanline(scaled_outline, (u32)pw, (u32)ph, 0, 0);

    GlyphBitmap gb;
    gb.width = (u32)pw;
    gb.height = (u32)ph;
    gb.bearing_x = metrics.bearing_x;
    gb.bearing_y = metrics.bearing_y;
    gb.advance_x = metrics.advance_x;
    gb.bitmap = std::move(bitmap);
    return Result<GlyphBitmap>(std::move(gb));
}

Result<GlyphBitmap> FontFace::rasterize_glyph_by_gid(u16 gid, u32 pixel_size) {
    if (gid == 0)
        return Result<GlyphBitmap>(std::string("glyph not in font"));

    int bezier_steps = (std::max)(4, (int)(pixel_size) / 4);
    auto outline_result = read_outline(gid, bezier_steps);
    if (outline_result.is_err())
        return Result<GlyphBitmap>(outline_result.unwrap_err());
    auto &outline = outline_result.unwrap();

    // Apply variable font gvar deltas if variation coords are set
    if (!variation_coords_.empty() && outline.num_contours > 0) {
        apply_gvar_deltas(gid, outline.points, outline.num_contours);
    }

    auto metrics_result = get_metrics_by_gid(gid, pixel_size);
    if (metrics_result.is_err())
        return Result<GlyphBitmap>(metrics_result.unwrap_err());
    auto &metrics = metrics_result.unwrap();

    f32 scale = (f32)pixel_size / units_per_em_;

    f32 x_min_px = (f32)outline.x_min * scale;
    f32 y_min_px = (f32)outline.y_min * scale;
    f32 x_max_px = (f32)outline.x_max * scale;
    f32 y_max_px = (f32)outline.y_max * scale;

    i32 pw = std::max(1, (i32)std::ceil(x_max_px - x_min_px) + 1);
    i32 ph = std::max(1, (i32)std::ceil(y_max_px - y_min_px) + 1);
    if (pw > 1024) pw = 1024;
    if (ph > 1024) ph = 1024;

    GlyphOutline scaled_outline;
    scaled_outline.num_contours = outline.num_contours;
    scaled_outline.contour_end_pts = outline.contour_end_pts;
    scaled_outline.points.resize(outline.points.size());
    for (size_t i = 0; i < outline.points.size(); i += 2) {
        f32 x = (outline.points[i] - (f32)outline.x_min) * scale;
        f32 y = ((f32)outline.y_max - outline.points[i + 1]) * scale;
        scaled_outline.points[i] = x;
        scaled_outline.points[i + 1] = y;
    }

    std::vector<u8> bitmap = rasterize_scanline(scaled_outline, (u32)pw, (u32)ph, 0, 0);

    GlyphBitmap gb;
    gb.width = (u32)pw;
    gb.height = (u32)ph;
    gb.bearing_x = metrics.bearing_x;
    gb.bearing_y = metrics.bearing_y;
    gb.advance_x = metrics.advance_x;
    gb.bitmap = std::move(bitmap);
    return Result<GlyphBitmap>(std::move(gb));
}

} // namespace browser::render
