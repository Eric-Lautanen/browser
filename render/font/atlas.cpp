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

        // Collect codepoints and detect if Arabic shaping is needed
        std::vector<u32> codepoints;
        const u8 *text_data = reinterpret_cast<const u8 *>(text.data());
        u32 text_offset = 0;
        u32 text_len = static_cast<u32>(text.size());
        bool needs_arabic = false;
        while (text_offset < text_len) {
            auto dr = html::decode_utf8(text_data + text_offset, text_len - text_offset);
            if (dr.bytes_consumed == 0) { text_offset++; continue; }
            text_offset += dr.bytes_consumed;
            u32 cp = (u32)dr.codepoint;
            codepoints.push_back(cp);
            if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
                (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
                (cp >= 0xFE70 && cp <= 0xFEFF))
                needs_arabic = true;
        }

        if (needs_arabic) {
            // Arabic text: use shaper for positional forms
            std::vector<ShapedGlyph> shaped;
            shaper_.shape(primary, codepoints, shaped, pixel_size);
            for (const auto &sg : shaped) {
                FontFace *gf = primary; u32 gid = sg.glyph_id; u32 cp = sg.codepoint;
                if (gid == 0 && fm_) {
                    FontFace *fb = fm_->find_font_for_codepoint(primary, cp);
                    if (fb && fb != primary) { gf = fb; gid = (u16)fb->glyph_index(cp); }
                }
                if (gid == 0) { cursor_x += (f32)sg.advance_x; continue; }
                GlyphAtlasEntry *gt = prepare_glyph_by_gid((u16)gid, cp, pixel_size, gf);
                if (!gt) { cursor_x += (f32)sg.advance_x; continue; }
                if (!textured_started || gt->page != current_page) {
                    r->begin_textured(&atlas_pages_[gt->page]); textured_started = true; current_page = gt->page;
                }
                Color gc = gt->has_color ? Color::WHITE : color;
                f32 gx = cursor_x + (f32)gt->bearing_x + (f32)sg.x_offset * ((f32)pixel_size / primary->units_per_em());
                f32 asc = gf->ascender(pixel_size);
                f32 gy = y + asc - (f32)gt->bearing_y + (f32)sg.y_offset * ((f32)pixel_size / primary->units_per_em());
                auto add_glyph = [&](f32 ox) {
                    if (is_italic) {
                        r->add_tex_quad_skewed(gx + ox, gy, (f32)gt->width, (f32)gt->height, (f32)pixel_size * 0.2f, gc, gt->u0, gt->v0, gt->u1, gt->v1);
                    } else {
                        r->add_tex_quad(gx + ox, gy, (f32)gt->width, (f32)gt->height, gc, gt->u0, gt->v0, gt->u1, gt->v1);
                    }
                };
                if (is_bold) { add_glyph(-0.5f); add_glyph(0.5f); }
                else { add_glyph(0); }
                cursor_x += (f32)sg.advance_x;
            }
        } else {
            // Non-Arabic: fast per-codepoint path with kern table kerning
            u32 prev_gid = 0; FontFace *prev_face = nullptr;
            for (u32 cp : codepoints) {
                u32 gid = primary->glyph_index(cp);
                FontFace *uf = primary;
                if (gid == 0 && fm_) {
                    FontFace *fb = fm_->find_font_for_codepoint(uf, cp);
                    if (fb) { uf = fb; gid = uf->glyph_index(cp); }
                }
                GlyphAtlasEntry *gt = prepare_glyph_by_gid((u16)gid, cp, pixel_size, uf);
                if (!gt) {
                    if (gid != 0) {
                        auto mr = uf->get_metrics_by_gid((u16)gid, pixel_size);
                        if (mr.is_ok()) cursor_x += (f32)mr.unwrap().advance_x;
                    }
                    continue;
                }
                if (uf && prev_face && prev_gid != 0 && prev_face == uf) {
                    i32 kern = uf->get_kerning((u16)prev_gid, (u16)gid);
                    if (kern) cursor_x += (f32)kern * ((f32)pixel_size / uf->units_per_em());
                }
                prev_gid = gid; prev_face = uf;
                if (!textured_started || gt->page != current_page) {
                    r->begin_textured(&atlas_pages_[gt->page]); textured_started = true; current_page = gt->page;
                }
                Color gc = gt->has_color ? Color::WHITE : color;
                f32 gx = cursor_x + (f32)gt->bearing_x;
                f32 asc = uf ? uf->ascender(pixel_size) : (f32)pixel_size * 0.8f;
                f32 gy = y + asc - (f32)gt->bearing_y;
                auto add_glyph = [&](f32 ox) {
                    if (is_italic) {
                        r->add_tex_quad_skewed(gx + ox, gy, (f32)gt->width, (f32)gt->height, (f32)pixel_size * 0.2f, gc, gt->u0, gt->v0, gt->u1, gt->v1);
                    } else {
                        r->add_tex_quad(gx + ox, gy, (f32)gt->width, (f32)gt->height, gc, gt->u0, gt->v0, gt->u1, gt->v1);
                    }
                };
                if (is_bold) { add_glyph(-0.5f); add_glyph(0.5f); }
                else { add_glyph(0); }
                cursor_x += (f32)gt->advance_x;
            }
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

        u16 gid = (u16)use_face->glyph_index(codepoint);
        return prepare_glyph_by_gid(gid, codepoint, pixel_size, use_face);
    }

    TextRenderer::GlyphAtlasEntry *TextRenderer::prepare_glyph_by_gid(u16 glyph_id,
                                                                      u32 codepoint,
                                                                      u32 pixel_size,
                                                                      FontFace *use_face) {
        if (!use_face || glyph_id == 0)
            return nullptr;

        CacheKey key{use_face, (u32)glyph_id, pixel_size};
        auto it = glyph_cache_.find(key);
        if (it != glyph_cache_.end())
            return &it->second;

        if (glyph_cache_.size() >= 8192)
            glyph_cache_.clear();

        // Try color bitmap first (emoji)
        GlyphBitmap gb;
        if (use_face->has_color_bitmap(codepoint, pixel_size)) {
            auto cr = use_face->rasterize_color_bitmap(codepoint);
            if (cr.is_ok()) {
                gb = std::move(cr.unwrap());
            }
        }

        if (gb.bitmap.empty()) {
            auto rr = use_face->rasterize_glyph_by_gid(glyph_id, pixel_size);
            if (rr.is_err()) {
                return nullptr;
            }
            gb = std::move(rr.unwrap());
        }

        if (gb.width == 0 || gb.height == 0)
            return nullptr;

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
                // Atlas full: evict oldest page (page 0) via LRU
                u32 evict_page = 0;
                for (auto it = glyph_cache_.begin(); it != glyph_cache_.end();) {
                    if (it->second.page == evict_page) {
                        it = glyph_cache_.erase(it);
                    } else {
                        ++it;
                    }
                }
                Texture2D page;
                std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
                auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
                if (r.is_err())
                    return nullptr;
                atlas_pages_[evict_page] = std::move(page);
                atlas_page_ = evict_page;
                atlas_cx_ = 0;
                atlas_cy_ = 0;
                atlas_row_h_ = 0;
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

    css::FontMetrics TextRenderer::get_font_metrics(u32 pixel_size) const {
        css::FontMetrics fm = {};
        FontFace *f = face_;
        if (!f) {
            fm.ascender = (f32)pixel_size * 0.8f;
            fm.descender = (f32)pixel_size * -0.2f;
            fm.line_gap = 0;
            return fm;
        }
        fm.ascender = f->ascender(pixel_size);
        fm.descender = f->descender(pixel_size);
        fm.line_gap = f->line_gap(pixel_size);
        return fm;
    }

    f32 TextRenderer::measure_text(const std::string &text, u32 pixel_size) {
        if (!face_) {
            return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
        }
        f32 total = 0.0f;
        const u8 *data = reinterpret_cast<const u8 *>(text.data());
        u32 offset = 0;
        u32 len = static_cast<u32>(text.size());
        u16 prev_gid = 0;
        FontFace *prev_face = nullptr;
        while (offset < len) {
            auto dr = html::decode_utf8(data + offset, len - offset);
            if (dr.bytes_consumed == 0) { offset++; continue; }
            offset += dr.bytes_consumed;
            u32 cp = dr.codepoint;
            FontFace *use_face = face_;
            if (use_face && use_face->glyph_index(cp) == 0 && fm_) {
                use_face = fm_->find_font_for_codepoint(use_face, cp);
            }
            if (!use_face) continue;
            u16 gid = (u16)use_face->glyph_index(cp);
            if (gid == 0) continue;
            CacheKey ck{use_face, (u32)gid, pixel_size};
            auto it = glyph_cache_.find(ck);
            if (it != glyph_cache_.end()) {
                total += (f32)it->second.advance_x;
            } else {
                auto mr = use_face->get_metrics_by_gid(gid, pixel_size);
                if (mr.is_ok())
                    total += (f32)mr.unwrap().advance_x;
            }
            if (use_face && prev_face && prev_gid != 0 && prev_face == use_face) {
                i32 kern = use_face->get_kerning(prev_gid, gid);
                if (kern != 0)
                    total += (f32)kern * ((f32)pixel_size / use_face->units_per_em());
            }
            prev_gid = gid;
            prev_face = use_face;
        }
        return total;
    }

}  // namespace browser::render