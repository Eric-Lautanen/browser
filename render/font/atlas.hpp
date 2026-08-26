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

        // Observability for tests/diagnostics.
        size_t atlas_page_count() const { return atlas_pages_.size(); }
        size_t glyph_cache_size() const { return glyph_cache_.size(); }

    private:
        static constexpr u32 kAtlasWidth = 1024;
        static constexpr u32 kAtlasHeight = 1024;
        static constexpr u32 kMaxAtlasPages = 4;
        // R-P6: raise from 2048 and evict least-recently-used halves instead
        // of nuking every entry (hot ASCII re-rasterized on the GL thread
        // caused visible jank on CJK-heavy pages).
        static constexpr size_t kGlyphCacheCap = 8192;

        Result<void> create_atlas();
        // R-P4/R-P5: single shared overflow path — clears BOTH glyph and
        // emoji caches together (stale UVs referenced overwritten pages) and
        // reclaims all atlas pages back to one instead of growing forever.
        void reset_atlas();

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
            u64 last_used = 0;  // R-P6: LRU stamp
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
        u64 cache_stamp_ = 0;

        // R-P5: emoji cache is per-renderer (was a function-local static — a
        // mutable global shared across instances) and cleared together with
        // the glyph cache on any atlas reset.
        struct EmojiCacheKey {
            const u8 *data;
            u32 pixel_size;
            bool operator==(const EmojiCacheKey &o) const { return data == o.data && pixel_size == o.pixel_size; }
        };
        struct EmojiCacheHash {
            u64 operator()(const EmojiCacheKey &k) const {
                return reinterpret_cast<u64>(k.data) ^ ((u64)k.pixel_size << 16);
            }
        };
        std::unordered_map<EmojiCacheKey, GlyphAtlasEntry, EmojiCacheHash> emoji_cache_;

        GlyphAtlasEntry *prepare_glyph(u16 glyph_id, u32 pixel_size, FontFace *face);
        GlyphAtlasEntry *prepare_emoji_glyph(u32 codepoint, const u8 *bitmap_data, u32 pixel_size);
    };

}  // namespace browser::render
