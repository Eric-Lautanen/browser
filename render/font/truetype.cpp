#include "font.hpp"
#include "image/format.hpp"
#include "image/decoder.hpp"
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

Result<void> FontFace::load_tables_from_offset(const u8 *data, u32 total_size, u32 font_offset) {
    const u8 *fp = data + font_offset;
    u32 fl = total_size - font_offset;

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

        u32 abs_off = font_offset + tab_off;
        if (abs_off > total_size || tab_len > total_size || abs_off + tab_len > total_size) continue;

        if (std::strcmp(tag, "cmap") == 0) { cmap_off_ = abs_off; cmap_len_ = tab_len; }
        else if (std::strcmp(tag, "head") == 0) { head_off_ = abs_off; head_len_ = tab_len; }
        else if (std::strcmp(tag, "hhea") == 0) { hhea_off_ = abs_off; hhea_len_ = tab_len; }
        else if (std::strcmp(tag, "maxp") == 0) { maxp_off_ = abs_off; maxp_len_ = tab_len; }
        else if (std::strcmp(tag, "hmtx") == 0) { hmtx_off_ = abs_off; hmtx_len_ = tab_len; }
        else if (std::strcmp(tag, "loca") == 0) { loca_off_ = abs_off; loca_len_ = tab_len; }
        else if (std::strcmp(tag, "glyf") == 0) { glyf_off_ = abs_off; glyf_len_ = tab_len; }
        else if (std::strcmp(tag, "kern") == 0) { kern_off_ = abs_off; kern_len_ = tab_len; }
        else if (std::strcmp(tag, "CBDT") == 0) { cbdt_off_ = abs_off; cbdt_len_ = tab_len;
        } else if (std::strcmp(tag, "CBLC") == 0) {
            cblc_off_ = abs_off;
            cblc_len_ = tab_len;
        } else if (std::strcmp(tag, "GSUB") == 0) {
            gsub_off_ = abs_off;
            gsub_len_ = tab_len;
        } else if (std::strcmp(tag, "GPOS") == 0) {
            gpos_off_ = abs_off;
            gpos_len_ = tab_len;
        } else if (std::strcmp(tag, "GDEF") == 0) {
            gdef_off_ = abs_off;
            gdef_len_ = tab_len;
        } else if (std::strcmp(tag, "fvar") == 0) {
            fvar_off_ = abs_off;
            fvar_len_ = tab_len;
        } else if (std::strcmp(tag, "gvar") == 0) {
            gvar_off_ = abs_off;
            gvar_len_ = tab_len;
        } else if (std::strcmp(tag, "HVAR") == 0) {
            hvar_off_ = abs_off;
            hvar_len_ = tab_len;
        }
    }

    // Parse fvar table to discover axes
    if (fvar_off_) {
        u32 fo = fvar_off_ - font_offset;
        if (fl >= fo + 16) {
            u16 axis_count = read_u16_be(fp, fo + 8, fl);
            fvar_axis_count_ = axis_count;
            fvar_axes_.reserve(axis_count);
            for (u16 i = 0; i < axis_count; i++) {
                u32 ao = fo + 16 + i * 20;
                if (ao + 20 > fl) break;
                AxisRecord ar;
                ar.tag = read_u32_be(fp, ao, fl);
                ar.min = (f32)(i16)read_i16_be(fp, ao + 4, fl) + (f32)read_u16_be(fp, ao + 6, fl) / 65536.0f;
                ar.def = (f32)(i16)read_i16_be(fp, ao + 8, fl) + (f32)read_u16_be(fp, ao + 10, fl) / 65536.0f;
                ar.max = (f32)(i16)read_i16_be(fp, ao + 12, fl) + (f32)read_u16_be(fp, ao + 14, fl) / 65536.0f;
                fvar_axes_.push_back(ar);
            }
        }
    }

    if (!head_off_ || !maxp_off_ || !hhea_off_) {
        return Result<void>(std::string("Missing required tables"));
    }

    units_per_em_ = (f32)read_u16_be(fp, head_off_ - font_offset + 18, fl);
    if (units_per_em_ < 16 || units_per_em_ > 16384) {
        return Result<void>(std::string("Invalid unitsPerEm"));
    }

    num_glyphs_ = read_u16_be(fp, maxp_off_ - font_offset + 4, fl);
    if (num_glyphs_ == 0) return Result<void>(std::string("No glyphs"));

    num_h_metrics_ = read_u16_be(fp, hhea_off_ - font_offset + 34, fl);
    if (num_h_metrics_ == 0) num_h_metrics_ = num_glyphs_;

    hhea_ascender_ = read_i16_be(fp, hhea_off_ - font_offset + 4, fl);
    hhea_descender_ = read_i16_be(fp, hhea_off_ - font_offset + 6, fl);
    hhea_line_gap_ = read_i16_be(fp, hhea_off_ - font_offset + 8, fl);

    long_loca_ = (read_i16_be(fp, head_off_ - font_offset + 50, fl) == 1);

    return {};
}

Result<void> FontFace::load_from_memory(const u8 *data, u32 size) {
    static u32 next_id = 1;
    font_id_ = next_id++;
    font_data_.assign(data, data + size);

    // Detect TTC (TrueType Collection)
    if (size >= 4 && std::memcmp(data, "ttcf", 4) == 0) {
        return load_from_ttc_memory(data, size, 0);
    }

    return load_tables_from_offset(font_data_.data(), (u32)font_data_.size(), 0);
}

Result<void> FontFace::load_from_ttc_memory(const u8 *data, u32 size, u32 font_index) {
    static u32 next_id = 1;
    font_id_ = next_id++;
    font_data_.assign(data, data + size);

    if (size < 12) return Result<void>(std::string("TTC too small"));

    u32 num_fonts = read_u32_be(data, 8, size);
    if (font_index >= num_fonts) {
        return Result<void>(std::string("TTC font index out of range"));
    }

    u32 font_offset = read_u32_be(data, 12 + font_index * 4, size);
    if (font_offset >= size) {
        return Result<void>(std::string("TTC font offset out of bounds"));
    }

    return load_tables_from_offset(font_data_.data(), (u32)font_data_.size(), font_offset);
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

// ── Variable font gvar delta application ────────────────────────────────

static f32 read_f2dot14_be(const u8* data, u32 off, u32 size) {
    return (f32)(i16)internal::read_u16_be(data, off, size) / 16384.0f;
}

void FontFace::apply_gvar_deltas(u16 gid, std::vector<f32>& points, u32 num_contours) const {
    if (!gvar_off_ || gvar_len_ < 20 || variation_coords_.empty())
        return;
    if (num_contours == 0 || points.size() < 4)
        return;

    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();
    u32 go = gvar_off_;

    if (go + 20 > fl) return;
    u16 axis_count = read_u16_be(fp, go + 4, fl);
    if (axis_count == 0) return;
    u16 shared_count = read_u16_be(fp, go + 6, fl);
    u32 data_off = go + read_u32_be(fp, go + 8, fl);

    // Read glyph offset array
    u32 off_size = ((data_off - go - 12) / 2 < (u32)(gid + 2)) ? 4 : 2;
    u32 off_base = go + 12;

    u32 glyph_data_off = 0;
    u32 next_glyph_off = 0;
    if (off_size == 4) {
        if (off_base + (gid + 2) * 4 > fl) return;
        glyph_data_off = read_u32_be(fp, off_base + gid * 4, fl);
        next_glyph_off = read_u32_be(fp, off_base + (gid + 1) * 4, fl);
    } else {
        if (off_base + (gid + 2) * 2 > fl) return;
        glyph_data_off = read_u16_be(fp, off_base + gid * 2, fl);
        next_glyph_off = read_u16_be(fp, off_base + (gid + 1) * 2, fl);
    }
    if (glyph_data_off == next_glyph_off) return;

    u32 abs_data_off = go + glyph_data_off;
    if (abs_data_off + 4 > fl) return;

    u16 tuple_count = read_u16_be(fp, abs_data_off, fl);
    if (tuple_count == 0) return;

    // Read shared tuples
    std::vector<f32> shared_coords;
    if (shared_count > 0) {
        u32 shared_off = go + 12 + (gid + 2) * off_size;
        // Round up to 4-byte boundary for shared coords
        shared_off = (shared_off + 3) & ~3;
        shared_coords.reserve(shared_count * axis_count);
        for (u32 i = 0; i < shared_count * axis_count; i++) {
            if (shared_off + i * 2 + 2 > fl) break;
            shared_coords.push_back(read_f2dot14_be(fp, shared_off + i * 2, fl));
        }
    }

    u32 th_base = abs_data_off + 4;
    u32 point_count = (u32)(points.size() / 2);
    u32 num_pts = point_count;

    for (u16 ti = 0; ti < tuple_count; ti++) {
        if (th_base + 4 > fl) break;
        u16 var_data_size = read_u16_be(fp, th_base, fl);
        u16 tuple_index = read_u16_be(fp, th_base + 2, fl);
        u32 th_size = 4;

        // Extract peak, start, end coordinates
        f32 peak[4] = {}, start[4] = {}, end[4] = {};
        u32 used_axes = axis_count > 4 ? 4 : axis_count;

        bool has_peak = (tuple_index & 0x8000) != 0;
        bool has_intermediate = (tuple_index & 0x4000) != 0;
        bool has_private_points = (tuple_index & 0x2000) != 0;
        u16 shared_idx = tuple_index & 0x0FFF;

        bool skip_tuple = false;

        if (has_peak) {
            for (u32 a = 0; a < used_axes; a++) {
                if (th_base + th_size + 2 > fl) { skip_tuple = true; break; }
                peak[a] = read_f2dot14_be(fp, th_base + th_size, fl);
                th_size += 2;
            }
        } else if (shared_idx < shared_count) {
            for (u32 a = 0; a < used_axes; a++)
                peak[a] = shared_coords[shared_idx * axis_count + a];
        } else {
            skip_tuple = true;
        }

        if (!skip_tuple && has_intermediate) {
            for (u32 a = 0; a < used_axes; a++) {
                if (th_base + th_size + 2 > fl) { skip_tuple = true; break; }
                start[a] = read_f2dot14_be(fp, th_base + th_size, fl);
                th_size += 2;
            }
            if (!skip_tuple) {
                for (u32 a = 0; a < used_axes; a++) {
                    if (th_base + th_size + 2 > fl) { skip_tuple = true; break; }
                    end[a] = read_f2dot14_be(fp, th_base + th_size, fl);
                    th_size += 2;
                }
            }
        } else if (!skip_tuple) {
            for (u32 a = 0; a < used_axes; a++) {
                start[a] = -1.0f;
                end[a] = 1.0f;
            }
        }

        if (!skip_tuple) {
            // Compute scalar for each axis, multiply together
            f32 scalar = 1.0f;
            for (u32 a = 0; a < used_axes; a++) {
                auto it = variation_coords_.find(fvar_axes_.size() > a ? fvar_axes_[a].tag : 0);
                f32 coord = it != variation_coords_.end() ? it->second : 0;
                if (coord < start[a] || coord > end[a]) { scalar = 0; break; }
                if (coord != peak[a]) {
                    if (coord < peak[a])
                        scalar *= (coord - start[a]) / (peak[a] - start[a]);
                    else
                        scalar *= (end[a] - coord) / (end[a] - peak[a]);
                }
            }
            if (scalar != 0) {
                // Read point numbers
                u32 serial_off = th_base + th_size;
                u32 serial_end = th_base + 4 + var_data_size;
                if (serial_end > fl) serial_end = fl;

                std::vector<u16> point_indices;
                if (has_private_points) {
                    u16 pt_count = 0;
                    if (serial_off + 2 > serial_end) goto skip_apply;
                    u8 first_byte = fp[serial_off];
                    if (first_byte & 0x80) {
                        pt_count = ((u16)(first_byte & 0x7F) << 8) | fp[serial_off + 1];
                        serial_off += 2;
                    } else {
                        pt_count = first_byte;
                        serial_off += 1;
                    }
                    u16 run_count = 0;
                    u16 cur_pt = 0;
                    while (point_indices.size() < pt_count && serial_off < serial_end) {
                        if (run_count == 0) {
                            u8 cnt = fp[serial_off++];
                            if (cnt & 0x80) {
                                run_count = cnt & 0x7F;
                            } else {
                                run_count = (cnt >> 4) + 1;
                                cur_pt += (cnt & 0x0F) + 1;
                                point_indices.push_back(cur_pt);
                                run_count--;
                                while (run_count > 0 && serial_off < serial_end) {
                                    cur_pt += fp[serial_off++] + 1;
                                    point_indices.push_back(cur_pt);
                                    run_count--;
                                }
                                continue;
                            }
                        }
                        if (serial_off >= serial_end) break;
                        u16 delta = fp[serial_off++];
                        cur_pt += delta;
                        point_indices.push_back(cur_pt);
                        run_count--;
                    }
                } else {
                    point_indices.resize(num_pts);
                    for (u32 p = 0; p < num_pts; p++)
                        point_indices[p] = (u16)p;
                }

                if (!point_indices.empty()) {
                    for (size_t pi = 0; pi < point_indices.size(); pi++) {
                        u16 pt = point_indices[pi];
                        if ((u32)(pt * 2 + 1) >= points.size()) break;
                        if (serial_off + 2 > serial_end) break;
                        f32 dx = read_f2dot14_be(fp, serial_off, fl);
                        f32 dy = read_f2dot14_be(fp, serial_off + 2, fl);
                        serial_off += 4;
                        points[pt * 2] += dx * scalar;
                        points[pt * 2 + 1] += dy * scalar;
                    }
                }
            }
        }

    skip_apply:;
        th_base += 4 + var_data_size;
    }
}

Result<FontFace::GlyphMetrics> FontFace::get_metrics_by_gid(u16 gid, u32 pixel_size) const {
    const u8* fp = font_data_.data();
    u32 fl = (u32)font_data_.size();
    if (units_per_em_ <= 0) return Result<GlyphMetrics>(std::string("invalid unitsPerEm"));
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

bool FontFace::has_color_bitmap(u32 codepoint, u32 pixel_size) const {
    if (!cblc_off_ || !cbdt_off_) return false;
    u32 gid = glyph_index(codepoint);
    if (gid == 0) return false;

    const u8 *fp = font_data_.data();
    u32 fl = (u32)font_data_.size();

    if (cblc_off_ + 4 > fl) return false;
    u16 num_sizes = read_u16_be(fp, cblc_off_ + 2, fl);

    u32 cblc_pos = cblc_off_ + 4;

    for (u16 s = 0; s < num_sizes; s++) {
        if (cblc_pos + 12 > fl) return false;
        u8 ppem_x = fp[cblc_pos];
        u8 ppem_y = fp[cblc_pos + 1];

        if ((u32)ppem_x != pixel_size && (u32)ppem_y != pixel_size)
        { cblc_pos += 8 + read_u32_be(fp, cblc_pos + 4, fl); continue; }

        u32 glyph_count = read_u32_be(fp, cblc_pos + 8, fl);
        cblc_pos += 12;

        for (u32 i = 0; i < glyph_count && cblc_pos + 8 <= fl; i++) {
            u16 glyph_id = read_u16_be(fp, cblc_pos, fl);
            u32 location = read_u32_be(fp, cblc_pos + 4, fl);
            if (glyph_id == gid) {
                u32 data_off = cbdt_off_ + location;
                if (data_off + 5 > fl) return false;
                u8 data_fmt = fp[data_off + 4];
                return (data_fmt == 17 || data_fmt == 18 || data_fmt == 19);
            }
            cblc_pos += 8;
        }
        break;
    }
    return false;
}

Result<GlyphBitmap> FontFace::rasterize_color_bitmap(u32 codepoint) const {
    if (!cblc_off_ || !cbdt_off_)
        return Result<GlyphBitmap>(std::string("No color bitmap tables"));

    u32 gid = glyph_index(codepoint);
    if (gid == 0) return Result<GlyphBitmap>(std::string("glyph not in font"));

    const u8 *fp = font_data_.data();
    u32 fl = (u32)font_data_.size();

    u16 num_sizes = read_u16_be(fp, cblc_off_ + 2, fl);
    u32 cblc_pos = cblc_off_ + 4;

    for (u16 s = 0; s < num_sizes; s++) {
        if (cblc_pos + 12 > fl)
            return Result<GlyphBitmap>(std::string("CBLC too small"));

        u32 glyph_count = read_u32_be(fp, cblc_pos + 8, fl);
        cblc_pos += 12;

        for (u32 i = 0; i < glyph_count && cblc_pos + 8 <= fl; i++) {
            u16 glyph_id = read_u16_be(fp, cblc_pos, fl);
            u32 location = read_u32_be(fp, cblc_pos + 4, fl);
            cblc_pos += 8;

            if (glyph_id != gid) continue;

            u32 data_off = cbdt_off_ + location;
            if (data_off + 9 > fl)
                return Result<GlyphBitmap>(std::string("CBDT too small"));

            u8 height = fp[data_off];
            i8 bearing_x = (i8)fp[data_off + 2];
            i8 bearing_y = (i8)fp[data_off + 3];
            u8 advance = fp[data_off + 4];
            u8 data_fmt = fp[data_off + 5];

            const u8 *png_start;
            u32 png_size;

            if (data_fmt == 17) {
                png_start = fp + data_off + 6;
                png_size = fl - (data_off + 6);
            } else if (data_fmt == 18) {
                png_start = fp + data_off + 9;
                png_size = fl - (data_off + 9);
            } else {
                return Result<GlyphBitmap>(std::string("Unsupported bitmap format"));
            }

            if (png_size < 8 || std::memcmp(png_start, "\x89PNG\r\n\x1a\n", 8) != 0)
                return Result<GlyphBitmap>(std::string("Invalid PNG in CBDT"));

            auto decoder = image::create_decoder(image::ImageFormat::PNG);
            if (!decoder)
                return Result<GlyphBitmap>(std::string("No PNG decoder"));

            auto img_r = decoder->decode(png_start, png_size);
            if (img_r.is_err())
                return Result<GlyphBitmap>(img_r.unwrap_err());

            auto img = std::move(img_r.unwrap());

            u32 bw = img.width;
            u32 bh = img.height;

            // Pack RGBA pixels into bitmap
            std::vector<u8> pixels(bw * bh * 4);
            if (img.rgba_pixels.size() >= bw * bh * 4) {
                std::memcpy(pixels.data(), img.rgba_pixels.data(), bw * bh * 4);
            } else {
                // Convert from RGB or other format
                for (u32 y = 0; y < bh && y * bw * 3 < (u32)img.rgba_pixels.size(); y++) {
                    for (u32 x = 0; x < bw; x++) {
                        u32 src_idx = (y * bw + x) * 3;
                        u32 dst_idx = (y * bw + x) * 4;
                        pixels[dst_idx] = img.rgba_pixels[src_idx];
                        pixels[dst_idx + 1] = img.rgba_pixels[src_idx + 1];
                        pixels[dst_idx + 2] = img.rgba_pixels[src_idx + 2];
                        pixels[dst_idx + 3] = 255;
                    }
                }
            }

            GlyphBitmap gb;
            gb.width = bw;
            gb.height = bh;
            gb.bearing_x = bearing_x;
            gb.bearing_y = (i32)(height) - (i32)bearing_y;
            gb.advance_x = (i32)advance * 16; // Scale to font units
            gb.has_color = true;
            gb.bitmap = std::move(pixels);
            return Result<GlyphBitmap>(std::move(gb));
        }
        break; // Only check first matching size
    }

    return Result<GlyphBitmap>(std::string("No bitmap found"));
}

} // namespace browser::render
