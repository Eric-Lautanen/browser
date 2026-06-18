#include "test_framework.hpp"
#include "../render/font.hpp"
#include "../render/embedded_font.hpp"
#include "../html/utf8.hpp"
#include "../render/font/registry.hpp"
#include "../render/font/shaper.hpp"

TEST(font_construct, {
    browser::render::FontFace face;
    ASSERT_EQ(face.units_per_em(), 0);
})

TEST(embedded_font_data, {
    ASSERT(browser::render::DEFAULT_FONT_DATA_SIZE > 0);
})

TEST(font_load, {
    const auto* fp = browser::render::DEFAULT_FONT_DATA;
    auto fl = browser::render::DEFAULT_FONT_DATA_SIZE;
    // Check magic
    if (fl < 12 || fp[0] != 0 || fp[1] != 1 || fp[2] != 0 || fp[3] != 0) {
        _err = "Bad sfVersion";
        return false;
    }
    browser::render::FontFace face;
    auto r = face.load_from_memory(fp, fl);
    if (r.is_err()) {
        _err = "load_from_memory failed: " + r.unwrap_err();
        return false;
    }
    auto upem = face.units_per_em();
    if (upem <= 0) {
        _err = "units_per_em is " + std::to_string(upem);
        return false;
    }
})

TEST(font_glyph_index, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    // Space (U+0020)
    ASSERT_EQ(face.glyph_index(0x0020), 1u);
    // 'A' (U+0041)
    ASSERT(face.glyph_index(0x0041) > 0);
})

TEST(font_metrics, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    auto mr = face.get_metrics(0x0041, 16);  // 'A' at 16px
    ASSERT(mr.is_ok());
    auto& m = mr.unwrap();
    ASSERT(m.advance_x > 0);
})

TEST(font_manager, {
    browser::render::FontManager fm;
    auto r = fm.load_default_font();
    ASSERT(r.is_ok());
    ASSERT(r.unwrap() != nullptr);
})

TEST(font_cache, {
    browser::render::FontManager fm;
    auto* face = fm.load_default_font().unwrap();
    auto* g = fm.get_or_rasterize(face, 'A', 16);
    ASSERT(g != nullptr);
    ASSERT(g->width > 0);
    ASSERT(g->height > 0);
})

TEST(font_rasterize, {
    browser::render::FontManager fm;
    auto* face = fm.load_default_font().unwrap();
    auto* g = fm.get_or_rasterize(face, 'A', 16);
    ASSERT(g != nullptr);
    ASSERT(g->width > 0);
    ASSERT(g->height > 0);
    bool has_pixels = false;
    for (browser::u32 i = 0; i < g->width * g->height; i++) {
        if (g->bitmap[i] > 0) { has_pixels = true; break; }
    }
    ASSERT(has_pixels);
})

TEST(font_ascender_descender, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    ASSERT(face.ascender(16) > 0);
    ASSERT(face.descender(16) < 0);
    ASSERT(face.line_gap(16) >= 0);
    browser::f32 ascent = face.ascender(16);
    browser::f32 descent = face.descender(16);
    ASSERT(ascent - descent > (browser::f32)0.0f);
})

TEST(font_rasterize_empty_glyph, {
    browser::render::FontManager fm;
    auto* face = fm.load_default_font().unwrap();
    auto* g = fm.get_or_rasterize(face, 0x0020, 16);
    ASSERT(g != nullptr);
})

TEST(font_rasterize_repeated, {
    browser::render::FontManager fm;
    auto* face = fm.load_default_font().unwrap();
    auto* g1 = fm.get_or_rasterize(face, 'A', 16);
    ASSERT(g1 != nullptr);
    auto* g2 = fm.get_or_rasterize(face, 'A', 16);
    ASSERT(g2 != nullptr);
    ASSERT(g1 == g2);
})

TEST(font_kerning, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    // Kerning may be 0 for many pairs, just ensure it doesn't crash
    browser::u32 gid_a = face.glyph_index('A');
    browser::u32 gid_v = face.glyph_index('V');
    if (gid_a > 0 && gid_v > 0) {
        auto kern = face.get_kerning((browser::u16)gid_a, (browser::u16)gid_v);
        ASSERT(kern >= -1000 && kern <= 1000);
    }
})

TEST(font_glyph_index_edge_ranges, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    // Test emoji-range codepoints (used to have debug I/O that could crash)
    ASSERT_EQ(face.glyph_index(0x2600), face.glyph_index(0x2600));  // ☀, no crash
    ASSERT_EQ(face.glyph_index(0x2700), face.glyph_index(0x2700));  // ✀, no crash
})

TEST(font_adaptive_bezier, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    // Small size should produce reasonable bezier steps
    auto small_glyph = face.rasterize_glyph('B', 10);
    ASSERT(small_glyph.is_ok());
    ASSERT(small_glyph.unwrap().width > 0);
})

TEST(font_get_metrics_by_gid, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    browser::u32 gid = face.glyph_index('A');
    ASSERT(gid > 0);
    auto mr = face.get_metrics_by_gid((browser::u16)gid, 16);
    ASSERT(mr.is_ok());
    ASSERT(mr.unwrap().advance_x > 0);
})

TEST(emoji_detection, {
    ASSERT(browser::html::is_emoji(0x1F600));  // grinning face
    ASSERT(browser::html::is_emoji(0x2600));   // sun
    ASSERT(browser::html::is_emoji(0x200D));   // ZWJ
    ASSERT(browser::html::is_emoji(0x1F1E6));  // regional indicator A
    ASSERT(browser::html::is_emoji(0xFE00));   // variation selector
    ASSERT(!browser::html::is_emoji(0x0041));  // 'A' is not emoji
    ASSERT(!browser::html::is_emoji(0x4E00));  // CJK is not emoji
    ASSERT(!browser::html::is_emoji(0x0600));  // Arabic is not emoji
})

TEST(fallback_for_missing_glyph, {
    browser::render::FontManager fm;
    fm.load_default_font();
    // Known codepoint returns a valid face
    auto* latin = fm.find_font_for_codepoint(nullptr, 0x0041);
    ASSERT(latin != nullptr);
    // Unknown codepoint with no preferred font returns nullptr
    auto* unknown = fm.find_font_for_codepoint(nullptr, 0x1F600);
    ASSERT(unknown == nullptr);
    // With preferred font, unknown codepoint returns the preferred font
    auto* preferred = fm.find_font_for_codepoint(latin, 0x1F600);
    ASSERT(preferred == latin);
})

TEST(web_font_takes_priority, {
    browser::render::FontManager fm;
    fm.load_default_font();
    auto* default_face = fm.find_font_by_name("");
    // Register a web font for a family name
    fm.register_web_font("MyWebFont", default_face);
    auto* resolved = fm.resolve_family("MyWebFont");
    ASSERT(resolved == default_face);
})

TEST(resolve_family_fallback_to_default, {
    browser::render::FontManager fm;
    fm.load_default_font();
    auto* face = fm.resolve_family("NONEXISTENT_FONT_XYZ");
    ASSERT(face != nullptr);  // Should return default font
})

TEST(registry_generic_mapping, {
    auto& reg = browser::render::FontRegistry::instance();
    reg.scan();
    std::string sans = reg.find_generic("sans-serif");
    ASSERT(!sans.empty());
    std::string serif = reg.find_generic("serif");
    ASSERT(!serif.empty());
    std::string mono = reg.find_generic("monospace");
    ASSERT(!mono.empty());
})

TEST(shaper_basic, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    browser::render::Shaper shaper;
    std::vector<unsigned int> cps = {0x0041, 0x0042};  // "AB"
    std::vector<browser::render::ShapedGlyph> out;
    shaper.shape(&face, cps, out, 16);
    ASSERT(!out.empty());
    ASSERT_EQ(out.size(), 2u);
    ASSERT(out[0].glyph_id > 0);
    ASSERT(out[0].face == &face);
    ASSERT(out[0].advance_x > 0);
})

TEST(shaper_preserves_codepoints, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    browser::render::Shaper shaper;
    std::vector<unsigned int> cps = {0x0041, 0x0042, 0x0043};  // "ABC"
    std::vector<browser::render::ShapedGlyph> out;
    shaper.shape(&face, cps, out, 16);
    ASSERT_EQ(out.size(), 3u);
    ASSERT_EQ(out[0].codepoint, 0x0041u);
    ASSERT_EQ(out[1].codepoint, 0x0042u);
    ASSERT_EQ(out[2].codepoint, 0x0043u);
})

TEST(glyph_rasterize_by_gid, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    browser::u16 gid = (browser::u16)face.glyph_index('A');
    ASSERT(gid > 0);
    auto gr = face.rasterize_glyph_by_gid(gid, 16);
    ASSERT(gr.is_ok());
    auto& gb = gr.unwrap();
    ASSERT(gb.width > 0);
    ASSERT(gb.height > 0);
    ASSERT(gb.advance_x > 0);
})

TEST(font_has_gsub_tables, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    // Default font may or may not have GSUB; verify accessors don't crash
    (void)face.gsub_off();
    (void)face.gsub_len();
    (void)face.gpos_off();
    (void)face.gpos_len();
})
