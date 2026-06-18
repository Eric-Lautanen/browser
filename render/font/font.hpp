#pragma once
#include "../../tests/utility.hpp"
#include "internal.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::render {

    struct GlyphBitmap {
        u32 width, height;
        i32 bearing_x, bearing_y;
        i32 advance_x;
        std::vector<u8> bitmap;
    };

    class FontFace {
    public:
        FontFace();
        ~FontFace();
        FontFace(const FontFace &) = delete;
        FontFace(FontFace &&) noexcept = default;
        FontFace &operator=(FontFace &&) noexcept = default;

        Result<void> load_from_memory(const u8 *data, u32 size);
        Result<GlyphBitmap> rasterize_glyph(u32 codepoint, u32 pixel_size);

        using GlyphMetrics = internal::GlyphMetrics;
        Result<GlyphMetrics> get_metrics(u32 codepoint, u32 pixel_size);
        f32 ascender(u32 pixel_size) const;
        f32 descender(u32 pixel_size) const;
        f32 line_gap(u32 pixel_size) const;
        f32 units_per_em() const { return units_per_em_; }
        u32 font_id() const { return font_id_; }

        u32 glyph_index(u32 codepoint) const;
        i32 get_kerning(u16 left_gid, u16 right_gid) const;
        Result<GlyphMetrics> get_metrics_by_gid(u16 gid, u32 pixel_size) const;

    private:
        struct TableRecord {
            char tag[4];
            u32 checksum;
            u32 offset;
            u32 length;
        };
        struct HeadTable {
            f32 units_per_em;
            i16 x_min, y_min, x_max, y_max;
            u16 glyph_data_format;
        };

        std::vector<u8> font_data_;
        u32 font_id_ = 0;
        u32 cmap_off_ = 0;
        u32 cmap_len_ = 0;
        u32 loca_off_ = 0;
        u32 loca_len_ = 0;
        u32 glyf_off_ = 0;
        u32 glyf_len_ = 0;
        u32 hmtx_off_ = 0;
        u32 hmtx_len_ = 0;
        u32 head_off_ = 0;
        u32 head_len_ = 0;
        u32 maxp_off_ = 0;
        u32 maxp_len_ = 0;
        u32 hhea_off_ = 0;
        u32 hhea_len_ = 0;
        u32 kern_off_ = 0;
        u32 kern_len_ = 0;

        f32 units_per_em_ = 0;
        i16 hhea_ascender_ = 0;
        i16 hhea_descender_ = 0;
        i16 hhea_line_gap_ = 0;
        u16 num_glyphs_ = 0;
        u16 num_h_metrics_ = 0;
        bool long_loca_ = false;

        static u16 read_u16_be(const u8 *data, u32 offset, u32 data_len);
        static u32 read_u32_be(const u8 *data, u32 offset, u32 data_len);
        static i16 read_i16_be(const u8 *data, u32 offset, u32 data_len);

        static Result<internal::GlyphOutline> parse_simple_glyph(const u8 *gdata,
                                                                 u32 gsize,
                                                                 i16 num_contours,
                                                                 int bezier_steps = 8);
        Result<internal::GlyphOutline> read_outline(u16 glyph_index, int bezier_steps = 8) const;

        static std::vector<u8> rasterize_scanline(
            const internal::GlyphOutline &outline, u32 pw, u32 ph, i32 offset_x, i32 offset_y);
    };

    class FontManager {
    public:
        FontManager();
        Result<FontFace *> load_default_font();
        Result<FontFace *> load_from_file(const std::string &path);
        FontFace *load_from_memory(const u8 *data, u32 size);
        GlyphBitmap *get_or_rasterize(FontFace *face, u32 codepoint, u32 pixel_size);

    private:
        FontFace *default_font_ = nullptr;
        std::vector<std::unique_ptr<FontFace>> fonts_;
        struct CacheKey {
            u32 face_id;
            u32 codepoint;
            u32 pixel_size;
            bool operator==(const CacheKey &o) const {
                return face_id == o.face_id && codepoint == o.codepoint && pixel_size == o.pixel_size;
            }
        };
        struct CacheHash {
            u64 operator()(const CacheKey &k) const {
                return (static_cast<u64>(k.face_id) << 48) ^ (static_cast<u64>(k.codepoint) << 16) ^ k.pixel_size;
            }
        };
        std::unordered_map<CacheKey, GlyphBitmap, CacheHash> cache_;
    };

}  // namespace browser::render
