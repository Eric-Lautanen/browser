#pragma once
#include "font.hpp"
#include "renderer.hpp"
#include "texture.hpp"
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace browser::render {

class TextRenderer {
public:
    TextRenderer(); ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&) noexcept;

    Result<void> initialize(FontManager* fm);
    void initialize(FontFace* face, FontManager* fm = nullptr);
    void set_font_manager(FontManager* fm) { fm_ = fm; }
    f32 render_text(Renderer* r, const std::string& text, f32 x, f32 y,
                    const Color& color, u32 pixel_size = 16);
    f32 measure_text(const std::string& text, u32 pixel_size = 16);

private:
    static constexpr u32 kAtlasWidth = 1024;
    static constexpr u32 kAtlasHeight = 1024;
    static constexpr u32 kMaxAtlasPages = 4;

    Result<void> create_atlas();

    FontManager* fm_ = nullptr;
    FontFace* face_ = nullptr;
    std::vector<Texture2D> atlas_pages_;
    u32 atlas_page_ = 0;
    u32 atlas_cx_ = 0, atlas_cy_ = 0, atlas_row_h_ = 0;

    struct GlyphAtlasEntry {
        f32 u0, v0, u1, v1;
        i32 bearing_x, bearing_y;
        i32 advance_x;
        u32 width, height;
        u32 page;
    };
    struct CacheKey { u32 codepoint; u32 pixel_size; bool operator==(const CacheKey& o) const { return codepoint == o.codepoint && pixel_size == o.pixel_size; } };
    struct CacheHash { u64 operator()(const CacheKey& k) const { return (u64)k.codepoint << 32 | k.pixel_size; } };
    std::unordered_map<CacheKey, GlyphAtlasEntry, CacheHash> glyph_cache_;
    GlyphAtlasEntry* prepare_glyph(u32 codepoint, u32 pixel_size);
};

}
