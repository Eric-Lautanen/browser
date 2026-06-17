#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../../tests/utility.hpp"

namespace browser::render::internal {

struct GlyphPoint {
    f32 x, y;
};

struct GlyphOutline {
    i16 num_contours;
    std::vector<u16> contour_end_pts;
    std::vector<f32> points;
    i16 x_min = 0, y_min = 0, x_max = 0, y_max = 0;
};

struct TTF_Table {
    char tag[4];
    u32 checksum;
    u32 offset;
    u32 length;
};

struct ParsedTables {
    u32 cmap_off = 0, cmap_len = 0;
    u32 loca_off = 0, loca_len = 0;
    u32 glyf_off = 0, glyf_len = 0;
    u32 hmtx_off = 0, hmtx_len = 0;
    u32 head_off = 0, head_len = 0;
    u32 maxp_off = 0, maxp_len = 0;
    u32 hhea_off = 0, hhea_len = 0;
    u32 kern_off = 0, kern_len = 0;
    f32 units_per_em = 0;
    i16 hhea_ascender = 0, hhea_descender = 0, hhea_line_gap = 0;
    u16 num_glyphs = 0, num_h_metrics = 0;
    bool long_loca = false;
};

struct GlyphMetrics {
    i32 width, height, bearing_x, bearing_y, advance_x, advance_y;
};

static inline u16 read_u16_be(const u8* data, u32 offset, u32 data_len) {
    if (offset > data_len || data_len - offset < 2) return 0;
    return (u16)data[offset] << 8 | (u16)data[offset + 1];
}

static inline u32 read_u32_be(const u8* data, u32 offset, u32 data_len) {
    if (offset > data_len || data_len - offset < 4) return 0;
    return (u32)data[offset] << 24 | (u32)data[offset + 1] << 16 |
           (u32)data[offset + 2] << 8 | (u32)data[offset + 3];
}

static inline i16 read_i16_be(const u8* data, u32 offset, u32 data_len) {
    return (i16)read_u16_be(data, offset, data_len);
}

} // namespace browser::render::internal
