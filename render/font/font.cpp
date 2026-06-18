#include "font.hpp"

#include "embedded.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
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
        return Result<FontFace *>(ptr);
    }

    FontFace *FontManager::load_from_memory(const u8 *data, u32 size) {
        auto face = std::make_unique<FontFace>();
        auto r = face->load_from_memory(data, size);
        if (r.is_err())
            return nullptr;
        FontFace *ptr = face.get();
        fonts_.push_back(std::move(face));
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
        fonts_.push_back(std::move(face));
        return Result<FontFace *>(ptr);
    }

    GlyphBitmap *FontManager::get_or_rasterize(FontFace *face, u32 codepoint, u32 pixel_size) {
        CacheKey key{face->font_id(), codepoint, pixel_size};
        auto it = cache_.find(key);
        if (it != cache_.end())
            return &it->second;
        auto result = face->rasterize_glyph(codepoint, pixel_size);
        if (result.is_err())
            return nullptr;
        auto [it2, inserted] = cache_.emplace(key, std::move(result.unwrap()));
        return &it2->second;
    }

}  // namespace browser::render
