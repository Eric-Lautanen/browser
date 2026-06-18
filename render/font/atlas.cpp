#include "atlas.hpp"

#include "../../html/utf8.hpp"
#include "../../platform/opengl.hpp"

namespace browser::render {

    namespace pgl = browser::platform;

    TextRenderer::TextRenderer() {
        glyph_cache_.reserve(2048);
    }
    TextRenderer::~TextRenderer() = default;
    TextRenderer::TextRenderer(TextRenderer &&) noexcept = default;

    Result<void> TextRenderer::create_atlas() {
        Texture2D page;
        std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
        auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
        if (r.is_err())
            return r;
        atlas_pages_.clear();
        atlas_pages_.push_back(std::move(page));
        atlas_page_ = 0;
        atlas_cx_ = 0;
        atlas_cy_ = 0;
        atlas_row_h_ = 0;
        return {};
    }

    Result<void> TextRenderer::initialize(FontManager *fm) {
        fm_ = fm;
        auto r = fm_->load_default_font();
        if (r.is_err())
            return Result<void>(r.unwrap_err());
        face_ = r.unwrap();
        return create_atlas();
    }

    void TextRenderer::initialize(FontFace *face, FontManager *fm) {
        face_ = face;
        fm_ = fm;
        create_atlas();
    }

    f32 TextRenderer::render_text(Renderer *r,
                                  const std::string &text,
                                  f32 x,
                                  f32 y,
                                  const Color &color,
                                  u32 pixel_size,
                                  u8 font_flags,
                                  const std::string &font_family) {
        f32 cursor_x = x;
        bool textured_started = false;
        u32 current_page = 0;
        const u8 *data = reinterpret_cast<const u8 *>(text.data());
        u32 offset = 0;
        u32 len = static_cast<u32>(text.size());
        u32 prev_gid = 0;
        FontFace *prev_face = nullptr;
        bool is_bold = (font_flags & 1) != 0;
        bool is_italic = (font_flags & 2) != 0;

        // Resolve CSS font-family to a physical font face
        FontFace *primary = face_;
        if (!font_family.empty() && fm_) {
            FontFace *resolved = fm_->resolve_family(font_family);
            if (resolved)
                primary = resolved;
        }

        if (!primary || atlas_pages_.empty()) {
            f32 tw = static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
            return tw;
        }

        while (offset < len) {
            auto dr = html::decode_utf8(data + offset, len - offset);
            if (dr.bytes_consumed == 0) {
                offset++;
                continue;
            }
            offset += dr.bytes_consumed;
            GlyphAtlasEntry *gt = prepare_glyph(dr.codepoint, pixel_size, primary);
            if (!gt) {
                FontFace *ef = primary;
                if (ef && ef->glyph_index(dr.codepoint) == 0 && fm_)
                    ef = fm_->find_font_for_codepoint(ef, dr.codepoint);
                if (ef && ef->glyph_index(dr.codepoint) != 0) {
                    auto mr = ef->get_metrics(dr.codepoint, pixel_size);
                    if (mr.is_ok())
                        cursor_x += (f32)mr.unwrap().advance_x;
                }
                continue;
            }

            FontFace *glyph_face = gt->face ? gt->face : primary;
            if (glyph_face && prev_face && prev_gid != 0 && prev_face == glyph_face) {
                u32 gid = glyph_face->glyph_index(dr.codepoint);
                i32 kern = glyph_face->get_kerning((u16)prev_gid, (u16)gid);
                cursor_x += (f32)kern * ((f32)pixel_size / glyph_face->units_per_em());
            }
            if (glyph_face) {
                prev_gid = glyph_face->glyph_index(dr.codepoint);
                prev_face = glyph_face;
            }

            if (!textured_started || gt->page != current_page) {
                r->begin_textured(&atlas_pages_[gt->page]);
                textured_started = true;
                current_page = gt->page;
            }

            Color glyph_color = gt->has_color ? Color::WHITE : color;

            f32 glyph_x = cursor_x + (f32)gt->bearing_x;
            f32 asc = glyph_face ? glyph_face->ascender(pixel_size) : (f32)pixel_size * 0.8f;
            f32 glyph_y = y + asc - static_cast<f32>(gt->bearing_y);

            if (is_bold && is_italic) {
                f32 skew = pixel_size * 0.2f;
                r->add_tex_quad_skewed(glyph_x - 0.5f,
                                       glyph_y,
                                       (f32)gt->width,
                                       (f32)gt->height,
                                       skew,
                                       glyph_color,
                                       gt->u0,
                                       gt->v0,
                                       gt->u1,
                                       gt->v1);
                r->add_tex_quad_skewed(glyph_x + 0.5f,
                                       glyph_y,
                                       (f32)gt->width,
                                       (f32)gt->height,
                                       skew,
                                       glyph_color,
                                       gt->u0,
                                       gt->v0,
                                       gt->u1,
                                       gt->v1);
            } else if (is_bold) {
                r->add_tex_quad(
                    glyph_x - 0.5f, glyph_y, (f32)gt->width, (f32)gt->height, glyph_color, gt->u0, gt->v0, gt->u1, gt->v1);
                r->add_tex_quad(
                    glyph_x + 0.5f, glyph_y, (f32)gt->width, (f32)gt->height, glyph_color, gt->u0, gt->v0, gt->u1, gt->v1);
            } else if (is_italic) {
                f32 skew = pixel_size * 0.2f;
                r->add_tex_quad_skewed(
                    glyph_x, glyph_y, (f32)gt->width, (f32)gt->height, skew, glyph_color, gt->u0, gt->v0, gt->u1, gt->v1);
            } else {
                r->add_tex_quad(
                    glyph_x, glyph_y, (f32)gt->width, (f32)gt->height, glyph_color, gt->u0, gt->v0, gt->u1, gt->v1);
            }
            cursor_x += (f32)gt->advance_x;
        }

        if (textured_started) {
            r->end_textured();
        }

        return cursor_x - x;
    }

    TextRenderer::GlyphAtlasEntry *TextRenderer::prepare_glyph(u32 codepoint,
                                                               u32 pixel_size,
                                                               FontFace *primary_override) {
        // Find the best face for this codepoint
        FontFace *use_face = primary_override ? primary_override : face_;
        if (use_face && use_face->glyph_index(codepoint) == 0 && fm_) {
            use_face = fm_->find_font_for_codepoint(use_face, codepoint);
        }

        if (!use_face)
            return nullptr;

        CacheKey key{use_face, codepoint, pixel_size};
        auto it = glyph_cache_.find(key);
        if (it != glyph_cache_.end())
            return &it->second;

        auto rasterize = [&]() -> Result<GlyphBitmap> {
            if (fm_) {
                auto *gb_ptr = fm_->get_or_rasterize(use_face, codepoint, pixel_size);
                if (gb_ptr)
                    return Result<GlyphBitmap>(*gb_ptr);
                return Result<GlyphBitmap>(std::string("glyph not found"));
            }
            return use_face->rasterize_glyph(codepoint, pixel_size);
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
                if (r.is_err())
                    return nullptr;
                atlas_pages_.push_back(std::move(page));
                atlas_cx_ = 0;
                atlas_cy_ = 0;
                atlas_row_h_ = 0;
            } else {
                return nullptr;
            }
        }

        atlas_pages_[atlas_page_].update_sub(atlas_cx_, atlas_cy_, w, h, gb.bitmap.data(), gb.has_color);

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
        entry.has_color = gb.has_color;
        entry.face = use_face;

        atlas_cx_ += w + kGutter;
        if (h + kGutter > atlas_row_h_)
            atlas_row_h_ = h + kGutter;

        auto [it2, inserted] = glyph_cache_.emplace(key, entry);
        return &it2->second;
    }

    f32 TextRenderer::measure_text(const std::string &text, u32 pixel_size) {
        if (!face_) {
            return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
        }
        f32 total = 0.0f;
        const u8 *data = reinterpret_cast<const u8 *>(text.data());
        u32 offset = 0;
        u32 len = static_cast<u32>(text.size());
        while (offset < len) {
            auto dr = html::decode_utf8(data + offset, len - offset);
            if (dr.bytes_consumed == 0) {
                offset++;
                continue;
            }
            offset += dr.bytes_consumed;
            u32 cp = dr.codepoint;
            FontFace *use_face = face_;
            if (use_face && use_face->glyph_index(cp) == 0 && fm_) {
                use_face = fm_->find_font_for_codepoint(use_face, cp);
            }
            CacheKey ck{use_face, cp, pixel_size};
            auto it = glyph_cache_.find(ck);
            if (it != glyph_cache_.end()) {
                total += (f32)it->second.advance_x;
            } else if (use_face) {
                if (use_face->glyph_index(cp) == 0)
                    continue;
                auto mr = use_face->get_metrics(cp, pixel_size);
                if (mr.is_ok()) {
                    total += (f32)mr.unwrap().advance_x;
                }
            }
        }
        return total;
    }

}  // namespace browser::render