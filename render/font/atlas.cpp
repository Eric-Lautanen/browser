#include "atlas.hpp"

#include "../../html/utf8.hpp"
#include "../../platform/opengl.hpp"

#include <unordered_map>

namespace browser::render {

    namespace pgl = browser::platform;

    TextRenderer::TextRenderer() {
        glyph_cache_.reserve(512);
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

    static GlyphBitmap make_emoji_bitmap(const u8 *data, u32 pixel_size) {
        GlyphBitmap gb;
        gb.width = EMOJI_W;
        gb.height = EMOJI_H;
        gb.bearing_x = 0;
        gb.bearing_y = (i32)pixel_size;
        gb.advance_x = (i32)pixel_size + 2;
        gb.is_sdf = false;
        gb.bitmap.assign(data, data + EMOJI_CELL);
        return gb;
    }

    TextRenderer::GlyphAtlasEntry *TextRenderer::prepare_glyph(u16 glyph_id, u32 pixel_size, FontFace *face) {
        if (!face || glyph_id == 0)
            return nullptr;

        GlyphCacheKey key{face, (u32)glyph_id, pixel_size};
        auto it = glyph_cache_.find(key);
        if (it != glyph_cache_.end())
            return &it->second;

        if (glyph_cache_.size() >= 2048)
            glyph_cache_.clear();

        auto rr = face->rasterize_glyph_by_gid(glyph_id, pixel_size);
        if (rr.is_err())
            return nullptr;
        auto &gb = rr.unwrap();

        if (gb.width == 0 || gb.height == 0)
            return nullptr;

        u32 w = gb.width, h = gb.height;
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
                atlas_cx_ = 0; atlas_cy_ = 0; atlas_row_h_ = 0;
            } else {
                glyph_cache_.clear();
                Texture2D page;
                std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
                auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
                if (r.is_err()) return nullptr;
                atlas_pages_[0] = std::move(page);
                atlas_page_ = 0;
                atlas_cx_ = 0; atlas_cy_ = 0; atlas_row_h_ = 0;
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
        entry.width = w;
        entry.height = h;
        entry.page = atlas_page_;
        entry.has_color = gb.has_color;
        entry.is_sdf = false;
        entry.glyph_id = glyph_id;
        entry.face = face;

        atlas_cx_ += w + kGutter;
        if (h + kGutter > atlas_row_h_)
            atlas_row_h_ = h + kGutter;

        auto [it2, inserted] = glyph_cache_.emplace(key, entry);
        return &it2->second;
    }

    TextRenderer::GlyphAtlasEntry *TextRenderer::prepare_emoji_glyph(u32 codepoint, const u8 *bitmap_data, u32 pixel_size) {
        (void)codepoint;
        struct EmojiKey {
            const u8 *data;
            u32 pixel_size;
            bool operator==(const EmojiKey &o) const { return data == o.data && pixel_size == o.pixel_size; }
        };
        struct EmojiHash {
            u64 operator()(const EmojiKey &k) const {
                return reinterpret_cast<u64>(k.data) ^ ((u64)k.pixel_size << 16);
            }
        };
        static std::unordered_map<EmojiKey, GlyphAtlasEntry, EmojiHash> emoji_cache;

        EmojiKey ek{bitmap_data, pixel_size};
        auto cit = emoji_cache.find(ek);
        if (cit != emoji_cache.end())
            return &cit->second;

        GlyphBitmap gb = make_emoji_bitmap(bitmap_data, pixel_size);
        u32 w = gb.width, h = gb.height;
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
                atlas_cx_ = 0; atlas_cy_ = 0; atlas_row_h_ = 0;
            } else {
                glyph_cache_.clear();
                emoji_cache.clear();
                Texture2D page;
                std::vector<u8> empty(kAtlasWidth * kAtlasHeight, 0);
                auto r = page.create(kAtlasWidth, kAtlasHeight, empty.data());
                if (r.is_err()) return nullptr;
                atlas_pages_[0] = std::move(page);
                atlas_page_ = 0;
                atlas_cx_ = 0; atlas_cy_ = 0; atlas_row_h_ = 0;
            }
        }

        atlas_pages_[atlas_page_].update_sub(atlas_cx_, atlas_cy_, w, h, gb.bitmap.data(), false);

        GlyphAtlasEntry entry;
        entry.u0 = (f32)atlas_cx_ / (f32)kAtlasWidth;
        entry.v0 = (f32)atlas_cy_ / (f32)kAtlasHeight;
        entry.u1 = (f32)(atlas_cx_ + w) / (f32)kAtlasWidth;
        entry.v1 = (f32)(atlas_cy_ + h) / (f32)kAtlasHeight;
        entry.bearing_x = gb.bearing_x;
        entry.bearing_y = gb.bearing_y;
        entry.advance_x = gb.advance_x;
        entry.width = w;
        entry.height = h;
        entry.page = atlas_page_;
        entry.has_color = false;
        entry.is_sdf = false;
        entry.glyph_id = 0;
        entry.face = nullptr;

        atlas_cx_ += w + kGutter;
        if (h + kGutter > atlas_row_h_)
            atlas_row_h_ = h + kGutter;

        auto [it2, inserted] = emoji_cache.emplace(ek, entry);
        return &it2->second;
    }

    f32 TextRenderer::render_text(Renderer *r,
                                  const std::string &text,
                                  f32 x,
                                  f32 y,
                                  const Color &color,
                                  u32 pixel_size,
                                  u8 font_flags) {
        f32 cursor_x = x;
        bool textured_started = false;
        u32 current_page = 0;
        bool is_bold = (font_flags & 1) != 0;

        if (!face_ || atlas_pages_.empty()) {
            return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
        }

        // Collect codepoints
        std::vector<u32> codepoints;
        const u8 *text_data = reinterpret_cast<const u8 *>(text.data());
        u32 text_offset = 0;
        u32 text_len = static_cast<u32>(text.size());
        while (text_offset < text_len) {
            auto dr = html::decode_utf8(text_data + text_offset, text_len - text_offset);
            if (dr.bytes_consumed == 0) {
                text_offset++;
                continue;
            }
            text_offset += dr.bytes_consumed;
            codepoints.push_back((u32)dr.codepoint);
        }

        u16 prev_gid = 0;
        for (u32 cp : codepoints) {
            // Emoji / TOFU path (coverage-based, not SDF)
            const u8 *emoji_data = find_emoji(cp);
            if (emoji_data || face_->glyph_index(cp) == 0) {
                if (!emoji_data)
                    emoji_data = EMOJI_TOFU;
                GlyphAtlasEntry *gt = prepare_emoji_glyph(cp, emoji_data, pixel_size);
                if (gt) {
                    if (!textured_started || gt->page != current_page) {
                        if (textured_started) r->end_textured();
                        r->begin_textured(&atlas_pages_[gt->page]);
                        textured_started = true;
                        current_page = gt->page;
                    }
                    f32 gx = cursor_x + (f32)gt->bearing_x;
                    f32 asc = face_ ? face_->ascender(pixel_size) : (f32)pixel_size * 0.8f;
                    f32 gy = y + asc - (f32)gt->bearing_y;
                    r->add_tex_quad(gx, gy, (f32)gt->width, (f32)gt->height, color, gt->u0, gt->v0, gt->u1, gt->v1);
                    cursor_x += (f32)gt->advance_x;
                }
                prev_gid = 0;
                continue;
            }

            // Font glyph path (coverage rasterized)
            u16 gid = (u16)face_->glyph_index(cp);
            GlyphAtlasEntry *gt = prepare_glyph(gid, pixel_size, face_);
            if (!gt) {
                auto mr = face_->get_metrics_by_gid(gid, pixel_size);
                if (mr.is_ok())
                    cursor_x += (f32)mr.unwrap().advance_x;
                continue;
            }

            if (prev_gid != 0) {
                i32 kern = face_->get_kerning(prev_gid, gid);
                if (kern)
                    cursor_x += (f32)kern * ((f32)pixel_size / face_->units_per_em());
            }
            prev_gid = gid;

            if (!textured_started || gt->page != current_page) {
                if (textured_started) r->end_textured();
                r->begin_textured(&atlas_pages_[gt->page]);
                textured_started = true;
                current_page = gt->page;
            }

            f32 gx = cursor_x + (f32)gt->bearing_x;
            f32 asc = face_ ? face_->ascender(pixel_size) : (f32)pixel_size * 0.8f;
            f32 gy = y + asc - (f32)gt->bearing_y;
            f32 gw = (f32)gt->width;
            f32 gh = (f32)gt->height;

            auto add_glyph = [&](f32 ox) {
                r->add_tex_quad(gx + ox, gy, gw, gh, color, gt->u0, gt->v0, gt->u1, gt->v1);
            };
            if (is_bold) {
                add_glyph(-0.5f);
                add_glyph(0.5f);
            } else {
                add_glyph(0);
            }
            cursor_x += (f32)gt->advance_x;
        }

        if (textured_started)
            r->end_textured();

        return cursor_x - x;
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
        while (offset < len) {
            auto dr = html::decode_utf8(data + offset, len - offset);
            if (dr.bytes_consumed == 0) {
                offset++;
                continue;
            }
            offset += dr.bytes_consumed;
            u32 cp = dr.codepoint;

            if (find_emoji(cp)) {
                total += (f32)pixel_size + 2.0f;
                prev_gid = 0;
                continue;
            }

            if (!face_)
                continue;
            u16 gid = (u16)face_->glyph_index(cp);
            if (gid == 0)
                continue;
            auto mr = face_->get_metrics_by_gid(gid, pixel_size);
            if (mr.is_ok())
                total += (f32)mr.unwrap().advance_x;
            if (prev_gid != 0) {
                i32 kern = face_->get_kerning(prev_gid, gid);
                if (kern != 0)
                    total += (f32)kern * ((f32)pixel_size / face_->units_per_em());
            }
            prev_gid = gid;
        }
        return total;
    }

}  // namespace browser::render
