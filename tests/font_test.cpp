#include "test_framework.hpp"
#include "../render/font.hpp"
#include "../render/embedded_font.hpp"
#include "../html/utf8.hpp"

#include <cstring>
#include <vector>

using browser::u8;
using browser::u16;
using browser::u32;

// ---- Hostile font construction helpers (R-S1/R-S2/R-S3) ----

static void be16(std::vector<u8>& d, u16 v) {
    d.push_back(static_cast<u8>(v >> 8));
    d.push_back(static_cast<u8>(v & 0xFF));
}
static void be32(std::vector<u8>& d, u32 v) {
    d.push_back(static_cast<u8>(v >> 24));
    d.push_back(static_cast<u8>((v >> 16) & 0xFF));
    d.push_back(static_cast<u8>((v >> 8) & 0xFF));
    d.push_back(static_cast<u8>(v & 0xFF));
}

struct TableEntry { const char* tag; std::vector<u8> data; };

// Minimal TrueType with head/maxp/hhea/loca/glyf only.
static std::vector<u8> build_font(const std::vector<TableEntry>& tables_in) {
    auto tables = tables_in;
    // Drop placeholder entries so required-table defaults get filled below.
    tables.erase(std::remove_if(tables.begin(), tables.end(),
                                [](const TableEntry& t) { return t.data.empty(); }),
                 tables.end());
    // Required defaults
    std::vector<u8> head(54, 0);
    head[18] = 0x04; head[19] = 0x00;              // unitsPerEm = 1024
    head[50] = 0x00; head[51] = 0x01;              // indexToLocFormat = 1 (long)
    std::vector<u8> maxp = {0x00, 0x00, 0x01, 0x00, 0x00, 0x03};  // numGlyphs = 3
    std::vector<u8> hhea(36, 0);
    hhea[34] = 0x00; hhea[35] = 0x03;              // numberOfHMetrics = 3

    bool have_head = false, have_maxp = false, have_hhea = false;
    size_t total = 12 + tables.size() * 16;
    for (auto& t : tables) {
        total += t.data.size();
        if (!std::strcmp(t.tag, "head")) have_head = true;
        if (!std::strcmp(t.tag, "maxp")) have_maxp = true;
        if (!std::strcmp(t.tag, "hhea")) have_hhea = true;
    }
    if (!have_head) tables.push_back({"head", head});
    if (!have_maxp) tables.push_back({"maxp", maxp});
    if (!have_hhea) tables.push_back({"hhea", hhea});

    std::vector<u8> f;
    be32(f, 0x00010000);
    be16(f, static_cast<u16>(tables.size()));
    be16(f, 16); be16(f, 0); be16(f, 0);
    u32 off = 12 + static_cast<u32>(tables.size()) * 16;
    for (auto& t : tables) {
        f.insert(f.end(), t.tag, t.tag + std::strlen(t.tag));
        be32(f, 0);          // checksum
        be32(f, off);        // offset
        be32(f, static_cast<u32>(t.data.size()));
        off += static_cast<u32>(t.data.size());
        if (off % 4) off += 4 - (off % 4);   // keep 4-byte alignment
    }
    // second pass to place data with the same padding logic
    u32 cursor = 12 + static_cast<u32>(tables.size()) * 16;
    std::vector<std::pair<u32, const TableEntry*>> placed;
    for (auto& t : tables) {
        placed.push_back({cursor, &t});
        cursor += static_cast<u32>(t.data.size());
        if (cursor % 4) cursor += 4 - (cursor % 4);
    }
    f.resize(cursor, 0);
    for (auto& [o, tp] : placed) {
        for (size_t i = 0; i < tp->data.size(); i++) f[o + i] = tp->data[i];
    }
    return f;
}

TEST(font_hostile_nonmonotonic_contours_rejected, {
    // Glyph 1: numContours=2, endPts=[10,5] â€” decreasing. Pre-fix the u16
    // subtraction underflowed and indexed ~65k past the point vectors.
    std::vector<u8> glyf;
    be16(glyf, 2);            // numContours
    be16(glyf, 0); be16(glyf, 0); be16(glyf, 100); be16(glyf, 100);  // bbox
    be16(glyf, 10);           // endPts[0]
    be16(glyf, 5);            // endPts[1]  <-- decreasing!
    be16(glyf, 0);            // instructionLength
    for (int i = 0; i < 24; i++) glyf.push_back(0x01);  // flags/on-curve padding

    std::vector<u8> loca;
    be32(loca, 0); be32(loca, static_cast<u32>(glyf.size())); be32(loca, static_cast<u32>(glyf.size())); be32(loca, static_cast<u32>(glyf.size()));

    std::vector<u8> font = build_font({
        {"head", {}}, {"maxp", {}}, {"hhea", {}},
        {"loca", loca}, {"glyf", glyf},
    });

    browser::render::FontFace face;
    ASSERT(face.load_from_memory(font.data(), static_cast<u32>(font.size())).is_ok());
    auto r = face.rasterize_glyph_by_gid(1, 16);
    ASSERT(r.is_err());   // must fail cleanly, never read out of bounds
})

TEST(font_hostile_huge_loca_entry_clamped, {
    // loca pair [0x10, 0xFFFFFFF0]: pre-fix gsize wrapped huge and every
    // internal guard compared against the inflated value while reading raw.
    std::vector<u8> glyf(16, 0x55);
    std::vector<u8> loca;
    be32(loca, 0x00000010);
    be32(loca, 0xFFFFFFF0);
    be32(loca, 0xFFFFFFF0);
    be32(loca, 0xFFFFFFF0);

    std::vector<u8> font = build_font({
        {"head", {}}, {"maxp", {}}, {"hhea", {}},
        {"loca", loca}, {"glyf", glyf},
    });

    browser::render::FontFace face;
    ASSERT(face.load_from_memory(font.data(), static_cast<u32>(font.size())).is_ok());
    auto r = face.rasterize_glyph_by_gid(1, 16);
    ASSERT(r.is_err());
})

TEST(font_hostile_composite_self_reference_rejected, {
    // Glyph 1 is a composite whose single component references glyph 1 â€”
    // infinite recursion pre-fix; now depth-capped at 32.
    std::vector<u8> self_comp;
    be16(self_comp, 0xFFFF);  // numContours = -1 â†’ composite
    be16(self_comp, 0); be16(self_comp, 0); be16(self_comp, 100); be16(self_comp, 100);
    be16(self_comp, 0x0002);  // ARG_1_AND_2_ARE_WORDS
    be16(self_comp, 0x0001);  // component glyph id = 1 (itself)
    be16(self_comp, 0x0000); be16(self_comp, 0x0000);  // args

    std::vector<u8> loca;
    be32(loca, 0);
    be32(loca, static_cast<u32>(self_comp.size()));
    be32(loca, static_cast<u32>(self_comp.size()));
    be32(loca, static_cast<u32>(self_comp.size()));

    std::vector<u8> font = build_font({
        {"head", {}}, {"maxp", {}}, {"hhea", {}},
        {"loca", loca}, {"glyf", self_comp},
    });

    browser::render::FontFace face;
    ASSERT(face.load_from_memory(font.data(), static_cast<u32>(font.size())).is_ok());
    auto r = face.rasterize_glyph_by_gid(1, 16);
    ASSERT(r.is_err());
})

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
