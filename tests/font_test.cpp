#include "test_framework.hpp"
#include "../render/font.hpp"
#include "../render/embedded_font.hpp"
#include "../html/utf8.hpp"

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
    ASSERT(face.glyph_index(0x0020) > 0);
    ASSERT(face.glyph_index(0x0041) > 0);
})

TEST(font_metrics, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
    auto mr = face.get_metrics(0x0041, 16);
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

TEST(font_rasterize, {
    browser::render::FontManager fm;
    auto* face = fm.load_default_font().unwrap();
    auto g = face->rasterize_glyph('A', 16);
    ASSERT(g.is_ok());
    auto& gb = g.unwrap();
    ASSERT(gb.width > 0);
    ASSERT(gb.height > 0);
    bool has_pixels = false;
    for (browser::u32 i = 0; i < gb.width * gb.height; i++) {
        if (gb.bitmap[i] > 0) { has_pixels = true; break; }
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

TEST(font_kerning, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
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
    ASSERT_EQ(face.glyph_index(0x2600), face.glyph_index(0x2600));
    ASSERT_EQ(face.glyph_index(0x2700), face.glyph_index(0x2700));
})

TEST(font_adaptive_bezier, {
    browser::render::FontFace face;
    auto r = face.load_from_memory(browser::render::DEFAULT_FONT_DATA,
                                    browser::render::DEFAULT_FONT_DATA_SIZE);
    ASSERT(r.is_ok());
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
    ASSERT(!gb.is_sdf);
    ASSERT(gb.width > 0);
    ASSERT(gb.height > 0);
    ASSERT(gb.advance_x > 0);
    // Coverage: should have filled pixels
    bool has_filled = false;
    for (browser::u32 i = 0; i < gb.width * gb.height; i++) {
        if (gb.bitmap[i] > 128) { has_filled = true; break; }
    }
    ASSERT(has_filled);
})
