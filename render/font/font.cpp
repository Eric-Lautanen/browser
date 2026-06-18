#include "font.hpp"

#include "embedded.hpp"

#include <algorithm>

namespace browser::render {

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

    Result<FontFace *> FontManager::load_default_font() {
        if (default_font_)
            return Result<FontFace *>(default_font_);
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

    void FontManager::add_font(FontFace *face) {
        all_fonts_.push_back(face);
    }

}  // namespace browser::render
