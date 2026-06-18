#include "atlas.hpp"
#include "../../platform/opengl.hpp"
#include "../../html/utf8.hpp"

namespace browser::render {

namespace pgl = browser::platform;

TextRenderer::TextRenderer() { glyph_cache_.reserve(2048); }
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
                               const Color& color, u32 pixel_size, u8 font_flags) {
    f32 cursor_x = x;
    bool textured_started = false;
    u32 current_page = 0;
    const u8* data = reinterpret_cast<const u8*>(text.data());
    u32 offset = 0;
    u32 len = static_cast<u32>(text.size());
    u32 prev_gid = 0;
    f32 scale = face_ ? (f32)pixel_size / face_->units_per_em() : 0.0f;
    bool is_bold = (font_flags & 1) != 0;
    bool is_italic = (font_flags & 2) != 0;

    if (!face_ || atlas_pages_.empty()) {
        f32 tw = static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
        return tw;
    }

    while (offset < len) {
        auto dr = html::decode_utf8(data + offset, len - offset);
        if (dr.bytes_consumed == 0) { offset++; continue; }
        offset += dr.bytes_consumed;
        GlyphAtlasEntry* gt = prepare_glyph(dr.codepoint, pixel_size);
        if (!gt) {
            // Advance cursor only if the glyph actually exists in the font
            if (face_ && face_->glyph_index(dr.codepoint) != 0) {
                auto mr = face_->get_metrics(dr.codepoint, pixel_size);
                if (mr.is_ok()) cursor_x += (f32)mr.unwrap().advance_x;
            }
            continue;
        }

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
        f32 asc = face_ ? face_->ascender(pixel_size) : (f32)pixel_size * 0.8f;
        f32 glyph_y = y + asc - static_cast<f32>(gt->bearing_y);

        if (is_bold && is_italic) {
            // Bold italic: slanted + double render
            f32 skew = pixel_size * 0.2f;
            r->add_tex_quad_skewed(glyph_x - 0.5f, glyph_y, (f32)gt->width, (f32)gt->height, skew,
                                   color, gt->u0, gt->v0, gt->u1, gt->v1);
            r->add_tex_quad_skewed(glyph_x + 0.5f, glyph_y, (f32)gt->width, (f32)gt->height, skew,
                                   color, gt->u0, gt->v0, gt->u1, gt->v1);
        } else if (is_bold) {
            // Faux bold: render twice with ±0.5px offset for thicker strokes
            r->add_tex_quad(glyph_x - 0.5f, glyph_y, (f32)gt->width, (f32)gt->height,
                            color, gt->u0, gt->v0, gt->u1, gt->v1);
            r->add_tex_quad(glyph_x + 0.5f, glyph_y, (f32)gt->width, (f32)gt->height,
                            color, gt->u0, gt->v0, gt->u1, gt->v1);
        } else if (is_italic) {
            // Faux italic: shear quad (top shifted right, bottom stationary)
            f32 skew = pixel_size * 0.2f;
            r->add_tex_quad_skewed(glyph_x, glyph_y, (f32)gt->width, (f32)gt->height, skew,
                                   color, gt->u0, gt->v0, gt->u1, gt->v1);
        } else {
            r->add_tex_quad(glyph_x, glyph_y, (f32)gt->width, (f32)gt->height,
                            color, gt->u0, gt->v0, gt->u1, gt->v1);
        }
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

    GlyphBitmap gb;
    auto r = rasterize();
    if (r.is_err()) {
        return nullptr;
    } else {
        gb = std::move(r.unwrap());
    }

    if (gb.width == 0 || gb.height == 0) {
        return nullptr;
    }

    u32 w = gb.width;
    u32 h = gb.height;
    constexpr u32 kGutter = 1;

    if (atlas_cx_ + w + kGutter > kAtlasWidth) {
        atlas_cx_ = 0;
        atlas_cy_ += atlas_row_h_;
        atlas_row_h_ = 0;
    }

    if (atlas_cy_ + h > kAtlasHeight) {
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
    if (!face_) {
        return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
    }
    f32 total = 0.0f;
    const u8* data = reinterpret_cast<const u8*>(text.data());
    u32 offset = 0;
    u32 len = static_cast<u32>(text.size());
    while (offset < len) {
        auto dr = html::decode_utf8(data + offset, len - offset);
        if (dr.bytes_consumed == 0) { offset++; continue; }
        offset += dr.bytes_consumed;
        u32 cp = dr.codepoint;
        CacheKey ck{cp, pixel_size};
        auto it = glyph_cache_.find(ck);
        if (it != glyph_cache_.end()) {
            total += (f32)it->second.advance_x;
        } else if (face_) {
            // Skip characters not actually present in the font (gid==0)
            if (face_->glyph_index(cp) == 0) continue;
            auto mr = face_->get_metrics(cp, pixel_size);
            if (mr.is_ok()) {
                total += (f32)mr.unwrap().advance_x;
            }
        }
    }
    return total;
}

} // namespace browser::render
