#include "font.hpp"

#include "embedded.hpp"
#include "registry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>

namespace browser::render {

    FontFace::FontFace() = default;
    FontFace::~FontFace() = default;

    Result<FontFace::GlyphMetrics> FontFace::get_metrics(u32 codepoint, u32 pixel_size) {
        u16 gid = (u16)glyph_index(codepoint);
        return get_metrics_by_gid(gid, pixel_size);
    }

    f32 FontFace::ascender(u32 pixel_size) const {
        if (hhea_ascender_ == 0 || units_per_em_ == 0)
            return (f32)pixel_size * 0.8f;
        return (f32)hhea_ascender_ * (f32)pixel_size / units_per_em_;
    }

    f32 FontFace::descender(u32 pixel_size) const {
        if (hhea_descender_ == 0 || units_per_em_ == 0)
            return (f32)pixel_size * -0.2f;
        return (f32)hhea_descender_ * (f32)pixel_size / units_per_em_;
    }

    f32 FontFace::line_gap(u32 pixel_size) const {
        if (hhea_line_gap_ == 0 || units_per_em_ == 0)
            return 0.0f;
        return (f32)hhea_line_gap_ * (f32)pixel_size / units_per_em_;
    }

    void FontFace::set_variation_coord(u32 axis_tag, f32 value) {
        // Normalize value to -1..1 using fvar axis range
        f32 normalized = 0;
        for (auto &a : fvar_axes_) {
            if (a.tag == axis_tag) {
                if (value < a.def)
                    normalized = (value - a.def) / (a.def - a.min);
                else if (value > a.def)
                    normalized = (value - a.def) / (a.max - a.def);
                break;
            }
        }
        variation_coords_[axis_tag] = normalized;
    }

    f32 FontFace::get_variation_coord(u32 axis_tag) const {
        auto it = variation_coords_.find(axis_tag);
        return it != variation_coords_.end() ? it->second : 0;
    }

    FontManager::FontManager() = default;

    Result<FontFace *> FontManager::load_default_font() {
        if (default_font_)
            return Result<FontFace *>(static_cast<FontFace *>(default_font_));
        auto face = std::make_unique<FontFace>();
        auto r = face->load_from_memory(DEFAULT_FONT_DATA, DEFAULT_FONT_DATA_SIZE);
        if (r.is_err())
            return Result<FontFace *>(r.unwrap_err());
        FontFace *ptr = face.get();
        fonts_.push_back(std::move(face));
        default_font_ = ptr;
        add_font(ptr);
        return Result<FontFace *>(ptr);
    }

    FontFace *FontManager::load_from_memory(const u8 *data, u32 size) {
        auto face = std::make_unique<FontFace>();
        auto r = face->load_from_memory(data, size);
        if (r.is_err())
            return nullptr;
        FontFace *ptr = face.get();
        fonts_.push_back(std::move(face));
        add_font(ptr);
        return ptr;
    }

    Result<FontFace *> FontManager::load_from_file(const std::string &path) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
            return Result<FontFace *>(std::string("Cannot open file: " + path));

        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        if (size <= 0) {
            std::fclose(f);
            return Result<FontFace *>(std::string("Empty font file"));
        }

        auto buf = std::make_unique<u8[]>((size_t)size);
        std::rewind(f);
        size_t read = std::fread(buf.get(), 1, (size_t)size, f);
        std::fclose(f);
        if ((long)read != size)
            return Result<FontFace *>(std::string("Failed to read font file"));

        auto face = std::make_unique<FontFace>();
        auto r = face->load_from_memory(buf.get(), (u32)size);
        if (r.is_err())
            return Result<FontFace *>(r.unwrap_err());

        FontFace *ptr = face.get();
        ptr->set_path(path);
        fonts_.push_back(std::move(face));
        add_font(ptr);
        return Result<FontFace *>(ptr);
    }

    void FontManager::add_font(FontFace *face) {
        all_fonts_.push_back(face);
    }

    FontFace *FontManager::find_font_by_name(const std::string &name) const {
        (void)name;
        return default_font_;
    }

    static bool font_has_glyph(FontFace *face, u32 codepoint) {
        return face && face->glyph_index(codepoint) != 0;
    }

    FontFace *FontManager::find_font_for_codepoint(FontFace *preferred, u32 codepoint) const {
        // First check preferred font
        if (preferred && font_has_glyph(preferred, codepoint))
            return preferred;

        // Walk fallback chain by unicode range
        for (const auto &fb : fallback_chain_) {
            if (codepoint >= fb.range_start && codepoint <= fb.range_end) {
                if (font_has_glyph(fb.face, codepoint))
                    return fb.face;
            }
        }

        // Try all loaded fonts as last resort
        for (auto *face : all_fonts_) {
            if (face != preferred && font_has_glyph(face, codepoint))
                return face;
        }

        return preferred;
    }

    void FontManager::load_fallback_fonts() {
        // Try to load CJK and emoji fonts from standard Windows locations.
        // These are loaded silently; failure is non-fatal.
        struct FontEntry {
            const char *path;
            u32 range_start;
            u32 range_end;
        };

        FontEntry entries[] = {
            {"C:\\Windows\\Fonts\\msgothic.ttc", 0x4E00, 0x9FFF},    // CJK Unified
            {"C:\\Windows\\Fonts\\msmincho.ttc", 0x4E00, 0x9FFF},    // CJK fallback
            {"C:\\Windows\\Fonts\\yugothm.ttc", 0x3040, 0x30FF},     // Japanese kana
            {"C:\\Windows\\Fonts\\seguiemj.ttf", 0x1F300, 0x1FAFF},  // Emoji
            {"C:\\Windows\\Fonts\\seguisym.ttf", 0x2000, 0x2BFF},    // Symbols
            {"C:\\Windows\\Fonts\\cour.ttf", 0x0000, 0x007F},        // ASCII monospace
            {"C:\\Windows\\Fonts\\times.ttf", 0x0000, 0x007F},       // Serif ASCII
        };

        // Also add a generic fallback on unicode ranges for already-loaded fonts
        // so that find_font_for_codepoint works even without Windows fonts
        for (auto *face : all_fonts_) {
            fallback_chain_.push_back({face, 0x0000, 0xFFFF});
        }

        for (const auto &entry : entries) {
            auto r = const_cast<FontManager *>(this)->load_from_file(entry.path);
            if (r.is_ok()) {
                fallback_chain_.push_back({r.unwrap(), entry.range_start, entry.range_end});
            }
        }
    }

    void FontManager::register_web_font(const std::string &family, FontFace *face) {
        if (face) web_fonts_[family] = face;
    }

    FontFace *FontManager::resolve_family(const std::string &family) {
        // Check web fonts (@font-face) first
        auto wf = web_fonts_.find(family);
        if (wf != web_fonts_.end())
            return wf->second;

        // Fall back to system fonts
        auto &reg = FontRegistry::instance();
        if (!reg.scanned())
            reg.scan();

        std::string path = reg.find_path(family);
        if (path.empty())
            path = reg.find_generic(family);
        if (path.empty())
            return default_font_;

        // Check if already loaded
        for (auto *f : all_fonts_) {
            if (f && f->path() == path)
                return f;
        }

        auto r = load_from_file(path);
        if (r.is_ok())
            return r.unwrap();
        return default_font_;
    }

    GlyphBitmap *FontManager::get_or_rasterize(FontFace *face, u32 codepoint, u32 pixel_size) {
        CacheKey key{face->font_id(), codepoint, pixel_size};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return &it->second;
        if (cache_.size() >= 4096)
            cache_.clear();
        auto result = face->rasterize_glyph(codepoint, pixel_size);
        if (result.is_err())
            return nullptr;
        auto [it2, inserted] = cache_.emplace(key, std::move(result.unwrap()));
        return &it2->second;
    }

}  // namespace browser::render
