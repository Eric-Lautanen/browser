#include "font.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace browser::render {

using internal::read_u16_be;
using internal::read_u32_be;
using internal::read_i16_be;
using internal::GlyphOutline;

u16 FontFace::read_u16_be(const u8* data, u32 offset, u32 data_len) {
    return internal::read_u16_be(data, offset, data_len);
}

u32 FontFace::read_u32_be(const u8* data, u32 offset, u32 data_len) {
    return internal::read_u32_be(data, offset, data_len);
}

i16 FontFace::read_i16_be(const u8* data, u32 offset, u32 data_len) {
    return internal::read_i16_be(data, offset, data_len);
}

Result<void> FontFace::load_from_memory(const u8* data, u32 size) {
    static u32 next_id = 1;
    font_id_ = next_id++;
    font_data_.assign(data, data + size);
    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();

    if (fl < 12) return Result<void>(std::string("Font too small"));

    u32 num_tables = read_u16_be(fp, 4, fl);
    if (num_tables > 100) return Result<void>(std::string("Too many tables"));

    for (u32 i = 0; i < num_tables; i++) {
        u32 dir_off = 12 + i * 16;
        if (dir_off + 16 > fl) break;

        char tag[5] = {};
        for (int j = 0; j < 4; j++) {
            tag[j] = (char)fp[dir_off + j];
        }
        u32 tab_off = read_u32_be(fp, dir_off + 8, fl);
        u32 tab_len = read_u32_be(fp, dir_off + 12, fl);

        if (tab_off > fl || tab_len > fl || tab_off + tab_len > fl) continue;

        if (std::strcmp(tag, "cmap") == 0) { cmap_off_ = tab_off; cmap_len_ = tab_len; }
        else if (std::strcmp(tag, "head") == 0) { head_off_ = tab_off; head_len_ = tab_len; }
        else if (std::strcmp(tag, "hhea") == 0) { hhea_off_ = tab_off; hhea_len_ = tab_len; }
        else if (std::strcmp(tag, "maxp") == 0) { maxp_off_ = tab_off; maxp_len_ = tab_len; }
        else if (std::strcmp(tag, "hmtx") == 0) { hmtx_off_ = tab_off; hmtx_len_ = tab_len; }
        else if (std::strcmp(tag, "loca") == 0) { loca_off_ = tab_off; loca_len_ = tab_len; }
        else if (std::strcmp(tag, "glyf") == 0) { glyf_off_ = tab_off; glyf_len_ = tab_len; }
        else if (std::strcmp(tag, "kern") == 0) { kern_off_ = tab_off; kern_len_ = tab_len; }
    }

    if (!head_off_ || !maxp_off_ || !hhea_off_) {
        return Result<void>(std::string("Missing required tables"));
    }

    units_per_em_ = (f32)read_u16_be(fp, head_off_ + 18, fl);
    if (units_per_em_ < 16 || units_per_em_ > 16384) {
        return Result<void>(std::string("Invalid unitsPerEm"));
    }

    num_glyphs_ = read_u16_be(fp, maxp_off_ + 4, fl);
    if (num_glyphs_ == 0) return Result<void>(std::string("No glyphs"));

    num_h_metrics_ = read_u16_be(fp, hhea_off_ + 34, fl);
    if (num_h_metrics_ == 0) num_h_metrics_ = num_glyphs_;

    hhea_ascender_ = read_i16_be(fp, hhea_off_ + 4, fl);
    hhea_descender_ = read_i16_be(fp, hhea_off_ + 6, fl);
    hhea_line_gap_ = read_i16_be(fp, hhea_off_ + 8, fl);

    long_loca_ = (read_i16_be(fp, head_off_ + 50, fl) == 1);

    return {};
}

u32 FontFace::glyph_index(u32 codepoint) const {
    if (!cmap_off_ || cmap_len_ < 4) return 0;
    const u8* cmap = font_data_.data() + cmap_off_;
    u16 num_tables = read_u16_be(cmap, 2, cmap_len_);

    for (u16 i = 0; i < num_tables; i++) {
        u32 pos = 4 + i * 8;
        if (pos > cmap_len_ || cmap_len_ - pos < 8) continue;
        u32 sub_off = read_u32_be(cmap, pos + 4, cmap_len_);
        if (sub_off >= cmap_len_) continue;

        u16 format = read_u16_be(cmap, sub_off, cmap_len_);
        u16 sub_len = read_u16_be(cmap, sub_off + 2, cmap_len_);
        if (sub_off + sub_len > cmap_len_) continue;

        if (format == 4) {
            u16 seg_count = read_u16_be(cmap, sub_off + 6, cmap_len_) / 2;
            if (seg_count == 0) continue;
            u32 end_base = sub_off + 14;
            u32 start_base = sub_off + 16 + seg_count * 2;
            u32 delta_base = sub_off + 16 + seg_count * 4;
            u32 range_base = sub_off + 16 + seg_count * 6;

            for (u16 s = 0; s < seg_count; s++) {
                u16 end = read_u16_be(cmap, end_base + s * 2, cmap_len_);
                if (codepoint > end) continue;
                u16 start = read_u16_be(cmap, start_base + s * 2, cmap_len_);
                if (codepoint < start) continue;

                i16 delta = read_i16_be(cmap, delta_base + s * 2, cmap_len_);
                u16 range_off = read_u16_be(cmap, range_base + s * 2, cmap_len_);

                if (range_off == 0) {
                    return (u16)((i32)codepoint + delta);
                } else {
                    u32 range_off_base = range_base + s * 2;
                    u16 raw = read_u16_be(cmap, range_off_base + range_off + (codepoint - start) * 2, cmap_len_);
                    u16 glyph_id = raw;
                    if (glyph_id != 0) glyph_id = (u16)(glyph_id + delta);
                    return glyph_id;
                }
            }
        } else if (format == 12) {
            u32 n_groups = read_u32_be(cmap, sub_off + 12, cmap_len_);
            for (u32 g = 0; g < n_groups; g++) {
                u32 gpos = sub_off + 16 + g * 12;
                u32 start_char = read_u32_be(cmap, gpos, cmap_len_);
                u32 end_char = read_u32_be(cmap, gpos + 4, cmap_len_);
                u32 start_gid = read_u32_be(cmap, gpos + 8, cmap_len_);
                if (codepoint >= start_char && codepoint <= end_char) {
                    return (u16)(start_gid + (codepoint - start_char));
                }
            }
        }
    }
    return 0;
}

Result<GlyphOutline> FontFace::parse_simple_glyph(
    const u8* gdata, u32 gsize, i16 num_contours, int bezier_steps)
{
    u16 end_pts_count = (u16)num_contours;
    std::vector<u16> contour_end_pts(end_pts_count);
    for (u16 i = 0; i < end_pts_count; i++) {
        contour_end_pts[i] = read_u16_be(gdata, 10 + i * 2, gsize);
    }

    u16 num_points = contour_end_pts.empty() ? 0 : (u16)(contour_end_pts.back() + 1);
    u16 instr_len = read_u16_be(gdata, 10 + end_pts_count * 2, gsize);
    u32 flags_off = 10 + end_pts_count * 2 + 2 + instr_len;

    std::vector<u8> flags(num_points);
    std::vector<f32> xs(num_points), ys(num_points);

    u32 fpos = flags_off;
    for (u16 i = 0; i < num_points; i++) {
        if (fpos >= gsize) break;
        u8 f = gdata[fpos];
        fpos++;
        flags[i] = f;
        if (f & 0x08) {
            if (fpos >= gsize) break;
            u8 repeat = gdata[fpos];
            fpos++;
            for (u8 r = 0; r < repeat && i + 1 + r < num_points; r++) {
                flags[i + 1 + r] = f;
            }
            i += repeat;
        }
    }

    f32 prev_x = 0;
    for (u16 i = 0; i < num_points; i++) {
        u8 f = flags[i];
        if (f & 0x02) {
            if (fpos >= gsize) break;
            u8 val = gdata[fpos];
            fpos++;
            prev_x += (f & 0x10) ? (f32)val : -(f32)val;
        } else if (!(f & 0x10)) {
            prev_x += (f32)read_i16_be(gdata, fpos, gsize);
            fpos += 2;
        }
        xs[i] = prev_x;
    }

    f32 prev_y = 0;
    for (u16 i = 0; i < num_points; i++) {
        u8 f = flags[i];
        if (f & 0x04) {
            if (fpos >= gsize) break;
            u8 val = gdata[fpos];
            fpos++;
            prev_y += (f & 0x20) ? (f32)val : -(f32)val;
        } else if (!(f & 0x20)) {
            prev_y += (f32)read_i16_be(gdata, fpos, gsize);
            fpos += 2;
        }
        ys[i] = prev_y;
    }

    std::vector<f32> all_pts;
    std::vector<u16> corrected_end_pts;

    for (u16 c = 0; c < end_pts_count; c++) {
        u16 end = contour_end_pts[c];
        u16 start = (c == 0) ? 0 : contour_end_pts[c - 1] + 1;
        u16 num_orig = end - start + 1;
        if (num_orig < 2) continue;

        struct Pt { f32 x, y; bool on; };
        std::vector<Pt> pts;
        for (u16 p = 0; p < num_orig; p++) {
            u16 idx = start + p;
            bool on_curve = (flags[idx] & 1) != 0;
            pts.push_back({xs[idx], ys[idx], on_curve});
            if (!on_curve) {
                u16 nxt = (p + 1 < num_orig) ? start + p + 1 : start;
                if (!(flags[nxt] & 1)) {
                    float mx = (xs[idx] + xs[nxt]) * 0.5f;
                    float my = (ys[idx] + ys[nxt]) * 0.5f;
                    pts.push_back({mx, my, true});
                }
            }
        }

        size_t n = pts.size();
        if (n < 2) continue;

        size_t first_on = 0;
        for (size_t p = 0; p < n; p++) {
            if (pts[p].on) { first_on = p; break; }
        }

        std::vector<f32> tess;
        size_t i = first_on;
        size_t processed = 0;
        while (processed < n) {
            size_t cur = i % n;
            if (!pts[cur].on) {
                i = (i + 1) % n;
                processed++;
                continue;
            }
            tess.push_back(pts[cur].x);
            tess.push_back(pts[cur].y);
            processed++;

            size_t nxt = (i + 1) % n;
            if (pts[nxt].on) {
                i = nxt;
            } else {
                size_t on2 = (nxt + 1) % n;
                float x0 = pts[cur].x, y0 = pts[cur].y;
                float cx = pts[nxt].x, cy = pts[nxt].y;
                float x2 = pts[on2].x, y2 = pts[on2].y;
                for (int s = 1; s <= bezier_steps; s++) {
                    float t = (float)s / bezier_steps;
                    float u = 1.0f - t;
                    tess.push_back(u*u*x0 + 2.0f*u*t*cx + t*t*x2);
                    tess.push_back(u*u*y0 + 2.0f*u*t*cy + t*t*y2);
                }
                i = on2;
                processed++;
            }
            if ((i % n) == first_on) break;
        }

        if (tess.size() / 2 < 2) continue;

        all_pts.insert(all_pts.end(), tess.begin(), tess.end());
        u16 pt_count = (u16)(tess.size() / 2);
        u16 prev_end = corrected_end_pts.empty() ? 0 : (corrected_end_pts.back() + 1);
        corrected_end_pts.push_back(prev_end + pt_count - 1);
    }

    GlyphOutline result;
    result.num_contours = (i16)corrected_end_pts.size();
    result.contour_end_pts = std::move(corrected_end_pts);
    result.points = std::move(all_pts);
    return Result<GlyphOutline>(std::move(result));
}

Result<GlyphOutline> FontFace::read_outline(u16 glyph_idx, int bezier_steps) const {
    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();

    u32 glyph_offset;
    u32 next_offset;
    if (long_loca_) {
        if (loca_off_ + (glyph_idx + 1) * 4 > fl) {
            return Result<GlyphOutline>(std::string("loca out of bounds"));
        }
        glyph_offset = read_u32_be(fp, loca_off_ + glyph_idx * 4, fl);
        next_offset = read_u32_be(fp, loca_off_ + (glyph_idx + 1) * 4, fl);
    } else {
        if (loca_off_ + (glyph_idx + 1) * 2 > fl) {
            return Result<GlyphOutline>(std::string("loca out of bounds"));
        }
        glyph_offset = (u32)read_u16_be(fp, loca_off_ + glyph_idx * 2, fl) * 2;
        next_offset = (u32)read_u16_be(fp, loca_off_ + (glyph_idx + 1) * 2, fl) * 2;
    }

    if (glyph_offset == next_offset) {
        return Result<GlyphOutline>(GlyphOutline{0, {}, {}, 0,0,0,0});
    }

    if (glyf_off_ + glyph_offset + 10 > fl) {
        return Result<GlyphOutline>(std::string("glyf out of bounds"));
    }

    const u8* gdata = fp + glyf_off_ + glyph_offset;
    u32 gsize = next_offset - glyph_offset;

    i16 num_contours = read_i16_be(gdata, 0, gsize);
    i16 x_min = read_i16_be(gdata, 2, gsize);
    i16 y_min = read_i16_be(gdata, 4, gsize);
    i16 x_max = read_i16_be(gdata, 6, gsize);
    i16 y_max = read_i16_be(gdata, 8, gsize);

    if (num_contours == 0) {
        return Result<GlyphOutline>(GlyphOutline{0, {}, {}, x_min, y_min, x_max, y_max});
    }

    if (num_contours > 0) {
        auto simple = parse_simple_glyph(gdata, gsize, num_contours, bezier_steps);
        if (simple.is_ok()) {
            auto result = std::move(simple.unwrap());
            result.x_min = x_min;
            result.y_min = y_min;
            result.x_max = x_max;
            result.y_max = y_max;
            return Result<GlyphOutline>(std::move(result));
        }
        return simple;
    }

    GlyphOutline result;
    result.num_contours = 0;
    result.x_min = x_min;
    result.y_min = y_min;
    result.x_max = x_max;
    result.y_max = y_max;

    u32 comp_off = 10;
    while (comp_off + 4 <= gsize) {
        u16 flags = read_u16_be(gdata, comp_off, gsize);
        u16 comp_gid = read_u16_be(gdata, comp_off + 2, gsize);
        comp_off += 4;

        i32 arg1 = 0, arg2 = 0;
        if (flags & 0x0002) {
            if (flags & 0x0001) {
                if (comp_off + 4 > gsize) break;
                arg1 = (i16)read_u16_be(gdata, comp_off, gsize);
                arg2 = (i16)read_u16_be(gdata, comp_off + 2, gsize);
                comp_off += 4;
            } else {
                if (comp_off + 2 > gsize) break;
                arg1 = (i8)gdata[comp_off];
                arg2 = (i8)gdata[comp_off + 1];
                comp_off += 2;
            }
        } else {
            if (flags & 0x0001) {
                if (comp_off + 4 > gsize) break;
                arg1 = read_u16_be(gdata, comp_off, gsize);
                arg2 = read_u16_be(gdata, comp_off + 2, gsize);
                comp_off += 4;
            } else {
                if (comp_off + 2 > gsize) break;
                arg1 = gdata[comp_off];
                arg2 = gdata[comp_off + 1];
                comp_off += 2;
            }
        }

        f32 a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        if (flags & 0x0008) {
            i16 scale = read_i16_be(gdata, comp_off, gsize);
            comp_off += 2;
            a = d = (f32)scale / 16384.0f;
        } else if (flags & 0x0080) {
            i16 ma = read_i16_be(gdata, comp_off, gsize);
            i16 mb = read_i16_be(gdata, comp_off + 2, gsize);
            i16 mc = read_i16_be(gdata, comp_off + 4, gsize);
            i16 md = read_i16_be(gdata, comp_off + 6, gsize);
            comp_off += 8;
            a = (f32)ma / 16384.0f;
            b = (f32)mb / 16384.0f;
            c = (f32)mc / 16384.0f;
            d = (f32)md / 16384.0f;
        } else if (flags & 0x0040) {
            i16 sx = read_i16_be(gdata, comp_off, gsize);
            i16 sy = read_i16_be(gdata, comp_off + 2, gsize);
            comp_off += 4;
            a = (f32)sx / 16384.0f;
            d = (f32)sy / 16384.0f;
        }

        auto comp_result = read_outline(comp_gid, bezier_steps);
        if (comp_result.is_err()) continue;
        auto& comp = comp_result.unwrap();
        if (comp.num_contours <= 0 && comp.points.empty()) continue;

        f32 dx = 0, dy = 0;
        if (flags & 0x0002) {
            dx = (f32)arg1;
            dy = (f32)arg2;
        } else {
            size_t parent_pt_idx = (size_t)arg1;
            size_t child_pt_idx = (size_t)arg2;
            if (parent_pt_idx * 2 + 1 < result.points.size() &&
                child_pt_idx * 2 + 1 < comp.points.size()) {
                f32 px = result.points[parent_pt_idx * 2];
                f32 py = result.points[parent_pt_idx * 2 + 1];
                f32 cx = comp.points[child_pt_idx * 2];
                f32 cy = comp.points[child_pt_idx * 2 + 1];
                dx = px - (a * cx + c * cy);
                dy = py - (b * cx + d * cy);
            }
        }

        size_t pt_offset = result.points.size() / 2;
        for (size_t i = 0; i < comp.points.size(); i += 2) {
            f32 px = comp.points[i];
            f32 py = comp.points[i + 1];
            result.points.push_back(a * px + c * py + dx);
            result.points.push_back(b * px + d * py + dy);
        }
        for (u16 ep : comp.contour_end_pts) {
            result.contour_end_pts.push_back((u16)(ep + pt_offset));
        }
        result.num_contours += comp.num_contours;

        if (!(flags & 0x0020)) break;
    }

    return Result<GlyphOutline>(std::move(result));
}

Result<FontFace::GlyphMetrics> FontFace::get_metrics_by_gid(u16 gid, u32 pixel_size) const {
    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();
    f32 scale = (f32)pixel_size / units_per_em_;

    u16 hmtx_idx = (std::min)(gid, (u16)(num_h_metrics_ - 1));
    if (!hmtx_off_ || hmtx_off_ + (hmtx_idx + 1) * 4 > fl) {
        return Result<GlyphMetrics>(std::string("hmtx out of bounds"));
    }
    u16 advance_width = read_u16_be(fp, hmtx_off_ + hmtx_idx * 4, fl);
    i16 lsb = read_i16_be(fp, hmtx_off_ + hmtx_idx * 4 + 2, fl);

    u32 glyph_offset;
    if (long_loca_) {
        glyph_offset = read_u32_be(fp, loca_off_ + gid * 4, fl);
    } else {
        glyph_offset = (u32)read_u16_be(fp, loca_off_ + gid * 2, fl) * 2;
    }

    i16 x_min = 0, y_min = 0, x_max = 0, y_max = 0;
    if (glyf_off_ + glyph_offset + 10 <= fl) {
        const u8* gdata = fp + glyf_off_ + glyph_offset;
        u32 gsize = fl - (glyf_off_ + glyph_offset);
        i16 num_contours = read_i16_be(gdata, 0, gsize);
        if (num_contours != 0) {
            x_min = read_i16_be(gdata, 2, gsize);
            y_min = read_i16_be(gdata, 4, gsize);
            x_max = read_i16_be(gdata, 6, gsize);
            y_max = read_i16_be(gdata, 8, gsize);
        }
    }

    GlyphMetrics m;
    i32 w = (i32)((f32)(x_max - x_min) * scale + 0.5f);
    i32 h = (i32)((f32)(y_max - y_min) * scale + 0.5f);
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w > 1024) w = 1024;
    if (h > 1024) h = 1024;
    m.width = w;
    m.height = h;
    m.bearing_x = (i32)((f32)lsb * scale + 0.5f);
    m.bearing_y = (i32)((f32)y_max * scale + 0.5f);
    m.advance_x = (i32)((f32)advance_width * scale + 0.5f);
    m.advance_y = 0;
    return Result<GlyphMetrics>(m);
}

i32 FontFace::get_kerning(u16 left_gid, u16 right_gid) const {
    if (!kern_off_ || kern_len_ < 4) return 0;
    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();
    u32 off = kern_off_;
    u32 end = kern_off_ + kern_len_;
    if (off + 4 > fl) return 0;
    u16 version = read_u16_be(fp, off, fl);
    if (version != 0) return 0;
    u16 n_tables = read_u16_be(fp, off + 2, fl);
    off += 4;
    for (u16 t = 0; t < n_tables && off + 6 <= end; t++) {
        if (off + 6 > fl) break;
        u16 sub_version = read_u16_be(fp, off, fl);
        u16 sub_len = read_u16_be(fp, off + 2, fl);
        u16 coverage = read_u16_be(fp, off + 4, fl);
        if (sub_version == 0 && (coverage & 0x0001) && !(coverage & 0x0002)) {
            u32 data_end = off + sub_len;
            off += 6;
            u16 n_pairs = read_u16_be(fp, off, fl);
            off += 8;
            u32 pair_size = 6;
            for (u16 p = 0; p < n_pairs && off + pair_size <= data_end && off + pair_size <= fl; p++) {
                u16 l = read_u16_be(fp, off, fl);
                u16 r = read_u16_be(fp, off + 2, fl);
                i16 val = read_i16_be(fp, off + 4, fl);
                if (l == left_gid && r == right_gid) return (i32)val;
                off += pair_size;
            }
        } else {
            off += sub_len;
        }
    }
    return 0;
}

} // namespace browser::render
