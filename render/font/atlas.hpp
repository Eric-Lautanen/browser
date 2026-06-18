#pragma once
#include "../../css/layout/types.hpp"
#include "../renderer.hpp"
#include "../texture.hpp"
#include "emoji.hpp"
#include "font.hpp"

#include <cstdio>
#include <unordered_map>
#include <vector>

namespace browser::render {

    class TextRenderer {
    public:
        TextRenderer();
        ~TextRenderer();
        TextRenderer(const TextRenderer &) = delete;
        TextRenderer(TextRenderer &&) noexcept;

        Result<void> initialize(FontManager *fm);
        void initialize(FontFace *face, FontManager *fm = nullptr);
        void set_font_face(FontFace *face, FontManager *fm) {
            face_ = face;
            fm_ = fm;
        }
        void set_font_manager(FontManager *fm) { fm_ = fm; }
        f32 render_text(Renderer *r,
                        const std::string &text,
                        f32 x,
                        f32 y,
                        const Color &color,
                        u32 pixel_size = 16,
                        u8 font_flags = 0);
        f32 measure_text(const std::string &text, u32 pixel_size = 16);
        css::FontMetrics get_font_metrics(u32 pixel_size) const;

    private:
        static constexpr u32 kAtlasWidth = 1024;
        static constexpr u32 kAtlasHeight = 1024;
        static constexpr u32 kMaxAtlasPages = 4;

        Result<void> create_atlas();

        FontManager *fm_ = nullptr;
        FontFace *face_ = nullptr;
        std::vector<Texture2D> atlas_pages_;
        u32 atlas_page_ = 0;
        u32 atlas_cx_ = 0, atlas_cy_ = 0, atlas_row_h_ = 0;

        struct GlyphAtlasEntry {
            f32 u0, v0, u1, v1;
            i32 bearing_x, bearing_y;
            i32 advance_x;
            u32 width, height;
            u32 page;
            bool has_color = false;
            bool is_sdf = false;
            u16 glyph_id = 0;
            FontFace *face = nullptr;
        };

        // Glyph cache: key = {face, glyph_id, pixel_size}
        struct GlyphCacheKey {
            FontFace *face;
            u32 glyph_id;
            u32 pixel_size;
            bool operator==(const GlyphCacheKey &o) const {
                return face == o.face && glyph_id == o.glyph_id && pixel_size == o.pixel_size;
            }
        };
        struct GlyphCacheHash {
            u64 operator()(const GlyphCacheKey &k) const {
                return (reinterpret_cast<u64>(k.face) << 32) ^ ((u64)k.glyph_id << 16) ^ k.pixel_size;
            }
        };
        std::unordered_map<GlyphCacheKey, GlyphAtlasEntry, GlyphCacheHash> glyph_cache_;

        GlyphAtlasEntry *prepare_glyph(u16 glyph_id, u32 pixel_size, FontFace *face);
        GlyphAtlasEntry *prepare_emoji_glyph(u32 codepoint, const u8 *bitmap_data, u32 pixel_size);
    };

}  // namespace browser::render
