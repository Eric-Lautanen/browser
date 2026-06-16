#include "text_renderer.hpp"
#include "../platform/opengl.hpp"
#include "../html/utf8.hpp"

namespace browser::render {

namespace pgl = browser::platform;

TextRenderer::TextRenderer() = default;
TextRenderer::~TextRenderer() = default;
TextRenderer::TextRenderer(TextRenderer&&) noexcept = default;

Result<void> TextRenderer::create_atlas() {
    Texture2D page;
    std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
    auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
    if (r.is_err()) return r;
    atlas_pages_.clear();
    atlas_pages_.push_back(std::move(page));
    atlas_page_ = 0;
    atlas_cx_ = 0;
    atlas_cy_ = 0;
    atlas_row_h_ = 0;
    return {};
}

Result<void> TextRenderer::initialize(FontManager* fm) {
    fm_ = fm;
    auto r = fm_->load_default_font();
    if (r.is_err()) return Result<void>(r.unwrap_err());
    face_ = r.unwrap();
    return create_atlas();
}

void TextRenderer::initialize(FontFace* face, FontManager* fm) {
    face_ = face;
    fm_ = fm;
    create_atlas();
}

f32 TextRenderer::render_text(Renderer* r, const std::string& text, f32 x, f32 y,
                               const Color& color, u32 pixel_size) {
    f32 cursor_x = x;
    bool textured_started = false;
    u32 current_page = 0;
    const u8* data = reinterpret_cast<const u8*>(text.data());
    u32 offset = 0;
    u32 len = static_cast<u32>(text.size());
    u32 prev_gid = 0;
    f32 scale = face_ ? (f32)pixel_size / face_->units_per_em() : 0.0f;

    while (offset < len) {
        auto dr = html::decode_utf8(data + offset, len - offset);
        if (dr.bytes_consumed == 0) { offset++; continue; }
        offset += dr.bytes_consumed;
        GlyphAtlasEntry* gt = prepare_glyph(dr.codepoint, pixel_size);
        if (!gt) continue;

        // Apply kerning
        if (face_ && prev_gid != 0) {
            u32 gid = face_->glyph_index(dr.codepoint);
            i32 kern = face_->get_kerning((u16)prev_gid, (u16)gid);
            cursor_x += (f32)kern * scale;
        }
        if (face_) prev_gid = face_->glyph_index(dr.codepoint);

        if (!textured_started || gt->page != current_page) {
            r->begin_textured(&atlas_pages_[gt->page]);
            textured_started = true;
            current_page = gt->page;
        }

        f32 glyph_x = cursor_x + (f32)gt->bearing_x;
        f32 glyph_y = y + (f32)pixel_size - (f32)gt->bearing_y;

        r->add_tex_quad(glyph_x, glyph_y, (f32)gt->width, (f32)gt->height,
                        color, gt->u0, gt->v0, gt->u1, gt->v1);
        cursor_x += (f32)gt->advance_x;
    }

    if (textured_started) {
        r->end_textured();
    }

    return cursor_x - x;
}

TextRenderer::GlyphAtlasEntry* TextRenderer::prepare_glyph(u32 codepoint, u32 pixel_size) {
    CacheKey key{codepoint, pixel_size};
    auto it = glyph_cache_.find(key);
    if (it != glyph_cache_.end()) return &it->second;

    auto rasterize = [&]() -> Result<GlyphBitmap> {
        if (!face_) return Result<GlyphBitmap>(std::string("no font face"));
        if (fm_) {
            auto* gb_ptr = fm_->get_or_rasterize(face_, codepoint, pixel_size);
            if (gb_ptr) return Result<GlyphBitmap>(*gb_ptr);
            return Result<GlyphBitmap>(std::string("glyph not found"));
        }
        return face_->rasterize_glyph(codepoint, pixel_size);
    };

    auto r = rasterize();
    if (r.is_err()) {
        if (codepoint > 32) {
            static FILE* f = fopen("glyph_drop.txt", "a");
            if (f) { fprintf(f, "drop: cp=0x%04X '%c' err=%s\n", codepoint, (codepoint < 128 ? (char)codepoint : '?'), r.unwrap_err().c_str()); fflush(f); fclose(f); }
        }
        return nullptr;
    }
    auto gb = std::move(r.unwrap());

    if (gb.width == 0 || gb.height == 0) {
        if (codepoint > 32) {
            static FILE* f = fopen("glyph_drop.txt", "a");
            if (f) { fprintf(f, "drop: cp=0x%04X '%c' w=%d h=%d\n", codepoint, (codepoint < 128 ? (char)codepoint : '?'), (int)gb.width, (int)gb.height); fflush(f); fclose(f); }
        }
        return nullptr;
    }

    // Pack into atlas with row-based shelf packing
    u32 w = gb.width;
    u32 h = gb.height;
    constexpr u32 kGutter = 1;

    if (atlas_cx_ + w + kGutter > kAtlasWidth) {
        atlas_cx_ = 0;
        atlas_cy_ += atlas_row_h_;
        atlas_row_h_ = 0;
    }

    if (atlas_cy_ + h > kAtlasHeight) {
        // Try next atlas page
        if (atlas_page_ + 1 < kMaxAtlasPages) {
            atlas_page_++;
            Texture2D page;
            std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
            auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
            if (r.is_err()) return nullptr;
            atlas_pages_.push_back(std::move(page));
            atlas_cx_ = 0;
            atlas_cy_ = 0;
            atlas_row_h_ = 0;
        } else {
            return nullptr;
        }
    }

    atlas_pages_[atlas_page_].update_sub(atlas_cx_, atlas_cy_, w, h, gb.bitmap.data());

    GlyphAtlasEntry entry;
    // UV coords use exact glyph bounds (no gutter in UV, gutter only in placement)
    entry.u0 = (f32)atlas_cx_ / (f32)kAtlasWidth;
    entry.v0 = (f32)atlas_cy_ / (f32)kAtlasHeight;
    entry.u1 = (f32)(atlas_cx_ + w) / (f32)kAtlasWidth;
    entry.v1 = (f32)(atlas_cy_ + h) / (f32)kAtlasHeight;
    entry.bearing_x = gb.bearing_x;
    entry.bearing_y = gb.bearing_y;
    entry.advance_x = gb.advance_x;
    entry.width = gb.width;
    entry.height = gb.height;
    entry.page = atlas_page_;

    atlas_cx_ += w + kGutter;
    if (h + kGutter > atlas_row_h_) atlas_row_h_ = h + kGutter;

    auto [it2, inserted] = glyph_cache_.emplace(key, entry);
    return &it2->second;
}

f32 TextRenderer::measure_text(const std::string& text, u32 pixel_size) {
    f32 total = 0.0f;
    const u8* data = reinterpret_cast<const u8*>(text.data());
    u32 offset = 0;
    u32 len = static_cast<u32>(text.size());
    while (offset < len) {
        auto dr = html::decode_utf8(data + offset, len - offset);
        if (dr.bytes_consumed == 0) { offset++; continue; }
        offset += dr.bytes_consumed;
        u32 cp = dr.codepoint;
        // Check cache first (avoids metrics lookup if already rasterized)
        CacheKey ck{cp, pixel_size};
        auto it = glyph_cache_.find(ck);
        if (it != glyph_cache_.end()) {
            total += (f32)it->second.advance_x;
        } else if (face_) {
            auto mr = face_->get_metrics(cp, pixel_size);
            if (mr.is_ok()) {
                total += (f32)mr.unwrap().advance_x;
            }
        }
    }
    return total;
}

}
