#include "../../render/icons.hpp"
#include "../bookmarks.hpp"
#include "../settings.hpp"
#include "window.hpp"

namespace browser {

    void BrowserWindow::render_nav_buttons() {
        using render::Icon;
        auto &t = theme_;
        constexpr f32 ICN = 14.0f;

        auto draw_btn = [&](const ChromeUI::ButtonRect &r, i32 id, Icon icon, bool enabled) {
            if (chrome_.hovered_button == id && enabled)
                renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
            render::Color c = enabled ? t.text : t.text_secondary;
            renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, ICN, c);
        };
        draw_btn(chrome_.rects.back, ChromeUI::BACK, Icon::BACK, chrome_.can_go_back);
        draw_btn(chrome_.rects.forward, ChromeUI::FORWARD, Icon::FORWARD, chrome_.can_go_forward);

        auto &rr = chrome_.rects.refresh;
        if (chrome_.hovered_button == ChromeUI::REFRESH)
            renderer_->fill_rect(rr.x, rr.y, rr.w, rr.h, t.surface_hover);

        if (chrome_.is_loading) {
            renderer_->fill_rect(rr.x, rr.y, rr.w, rr.h, {0.9f, 0.3f, 0.3f, 0.15f});
            renderer_->draw_icon_centered(Icon::STOP, rr.x, rr.y, rr.w, rr.h, ICN, t.text);
        } else {
            renderer_->draw_icon_centered(Icon::REFRESH, rr.x, rr.y, rr.w, rr.h, ICN, t.text);
        }
    }

    void BrowserWindow::render_address_bar() {
        auto &t = theme_;
        auto &r = chrome_.rects.address;

        renderer_->stroke_rect(r.x - 1,
                               r.y - 1,
                               r.w + 2,
                               r.h + 2,
                               chrome_.address_focused ? t.accent : t.border,
                               chrome_.address_focused ? 2.0f : 1.0f);
        renderer_->fill_rect(r.x, r.y, r.w, r.h, chrome_.address_focused ? t.surface : t.surface_hover);

        // Zoom indicator (right side of address bar)
        f32 zoom = settings_ ? settings_->zoom_level() : 1.0f;
        if (zoom != 1.0f) {
            char zoom_text[16];
            snprintf(zoom_text, sizeof(zoom_text), "%d%%", static_cast<int>(zoom * 100.0f));
            f32 zw = text_renderer_->measure_text(zoom_text, 11);
            f32 zx = r.x + r.w - zw - 6;
            f32 zy = r.y + 5;
            text_renderer_->render_text(renderer_.get(), zoom_text, zx, zy, t.accent, 11);
        }

        std::string text = chrome_.address_focused ? chrome_.address_bar.edit_buffer : chrome_.url;
        f32 tx = r.x + 6;
        f32 ty = r.y + 4;

        // Security indicator via tint
        if (!chrome_.address_focused && chrome_.is_https) {
            if (chrome_.has_mixed_content) {
                render::Color warning = {0.9f, 0.6f, 0.0f, 0.15f};
                renderer_->fill_rect(r.x, r.y, r.w, r.h, warning);
            } else {
                render::Color secure = {0.0f, 0.6f, 0.2f, 0.08f};
                renderer_->fill_rect(r.x, r.y, r.w, r.h, secure);
            }
        }

        // Selection highlight (whole buffer or just a double-clicked word)
        if (chrome_.address_focused && chrome_.address_bar.has_selection() && !text.empty()) {
            u32 a = std::min(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
            u32 b = std::max(chrome_.address_bar.sel_start, chrome_.address_bar.cursor_pos);
            if (a > text.size())
                a = static_cast<u32>(text.size());
            if (b > text.size())
                b = static_cast<u32>(text.size());
            f32 sel_x = tx + text_renderer_->measure_text(text.substr(0, a), 13);
            f32 sel_w = text_renderer_->measure_text(text.substr(a, b - a), 13);
            renderer_->fill_rect(sel_x, ty, sel_w, 16, {0.2f, 0.4f, 0.9f, 0.25f});
        }

        text_renderer_->render_text(renderer_.get(), text, tx, ty, t.text, 13);

        if (chrome_.address_focused && !chrome_.address_bar.has_selection()) {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            u64 elapsed = ms - chrome_.blink_start_ms;
            if ((elapsed / 500) % 2 == 0) {
                f32 cx = tx + text_renderer_->measure_text(
                                  chrome_.address_bar.edit_buffer.substr(0, chrome_.address_bar.cursor_pos), 13);
                renderer_->draw_line(cx, ty, cx, ty + 16, t.text, 1.5f);
            }
        }

        // Indeterminate loading progress: an accent segment slides across the
        // bottom edge of the omnibox while a page loads. Frames tick every
        // ~16 ms while is_loading (run loop keeps the beat).
        if (chrome_.is_loading) {
            auto now = std::chrono::steady_clock::now();
            u64 ms =
                static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            f32 cycle = static_cast<f32>(ms % 1400) / 1400.0f;
            f32 seg_frac = 0.3f;
            f32 seg_w = r.w * seg_frac;
            f32 seg_x = r.x + (r.w - seg_w) * cycle;
            renderer_->fill_rect(seg_x, r.y + r.h - 2.0f, seg_w, 2.0f, t.accent);
        }
    }

    void BrowserWindow::render_download_button() {
        auto &t = theme_;
        auto &r = chrome_.rects.download;

        // Only visible when there are download items
        if (download_manager_->items().empty())
            return;

        if (chrome_.hovered_button == ChromeUI::DOWNLOAD)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        // Download arrow: shaft + head + baseline
        f32 cx = r.x + r.w * 0.5f;
        f32 cy = r.y + r.h * 0.5f;
        render::Color c = t.text;
        renderer_->draw_line(cx, cy - 5.0f, cx, cy + 2.5f, c, 1.5f);
        renderer_->draw_line(cx - 3.5f, cy - 0.5f, cx, cy + 3.0f, c, 1.5f);
        renderer_->draw_line(cx, cy + 3.0f, cx + 3.5f, cy - 0.5f, c, 1.5f);
        renderer_->draw_line(cx - 4.0f, cy + 5.5f, cx + 4.0f, cy + 5.5f, c, 1.5f);
        // Show active download count badge
        u32 active = download_manager_->active_count();
        if (active > 0) {
            renderer_->fill_rect(r.x + r.w - 10, r.y - 2, 12, 12, {0.9f, 0.2f, 0.2f, 1.0f});
            char badge[4];
            snprintf(badge, sizeof(badge), "%u", active);
            text_renderer_->render_text(renderer_.get(), badge, r.x + r.w - 8, r.y - 1, {1.0f, 1.0f, 1.0f, 1.0f}, 9);
        }
    }

    void BrowserWindow::render_bookmark() {
        using render::Icon;
        auto &t = theme_;
        auto &r = chrome_.rects.bookmark;
        auto &cr = chrome_.rects.bookmark_chevron;
        if (chrome_.hovered_button == ChromeUI::BOOKMARK)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        if (chrome_.hovered_button == ChromeUI::BOOKMARK_CHEVRON)
            renderer_->fill_rect(cr.x, cr.y, cr.w, cr.h, t.surface_hover);
        Icon icon = chrome_.is_bookmarked ? Icon::BOOKMARK_ON : Icon::BOOKMARK_OFF;
        render::Color c = chrome_.is_bookmarked ? t.bookmark_fill : t.text_secondary;
        renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, 14.0f, c);
        // Draw small downward chevron next to the star
        f32 cy = cr.y + cr.h * 0.5f;
        f32 cx = cr.x + cr.w * 0.5f;
        f32 s = 3.0f;
        renderer_->draw_line(cx - s, cy - s * 0.4f, cx, cy + s * 0.6f, t.text_secondary, 1.5f);
        renderer_->draw_line(cx, cy + s * 0.6f, cx + s, cy - s * 0.4f, t.text_secondary, 1.5f);
    }

    void BrowserWindow::render_menu_button() {
        auto &t = theme_;
        auto &r = chrome_.rects.menu;
        if (chrome_.hovered_button == ChromeUI::MENU)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        renderer_->draw_icon_centered(render::Icon::MENU, r.x, r.y, r.w, r.h, 14.0f, t.text);
    }

    void BrowserWindow::render_menu() {
        if (!chrome_.show_menu)
            return;
        auto &t = theme_;
        auto geom = ChromeUI::menu_geometry(
            static_cast<f32>(viewport_width_), static_cast<f32>(viewport_height_), chrome_height(), chrome_.rects.menu);
        const f32 dx = geom.x, dy = geom.y, mw = geom.w, mh = geom.h;
        constexpr f32 item_h = ChromeUI::MENU_ITEM_H;
        constexpr f32 pad = 4.0f;

        renderer_->fill_rect(dx + 3.0f, dy + 3.0f, mw, mh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 1.5f});
        renderer_->fill_rect(dx + 1.5f, dy + 1.5f, mw, mh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 0.8f});

        renderer_->fill_rect(dx, dy, mw, mh, t.surface);
        renderer_->stroke_rect(dx, dy, mw, mh, t.border, 1.0f);

        f32 arrow_cx = chrome_.rects.menu.x + chrome_.rects.menu.w * 0.5f;
        f32 arrow_cy = dy;
        f32 arrow_sz = 5.0f;
        if (dy > chrome_height()) {
            renderer_->draw_line(arrow_cx - arrow_sz, arrow_cy, arrow_cx, arrow_cy - arrow_sz, t.surface, 2.0f);
            renderer_->draw_line(arrow_cx, arrow_cy - arrow_sz, arrow_cx + arrow_sz, arrow_cy, t.surface, 2.0f);
            renderer_->draw_line(arrow_cx - arrow_sz, arrow_cy, arrow_cx + arrow_sz, arrow_cy, t.surface, 2.0f);
        }

        struct MenuItem {
            const char *label;
            const char *shortcut;
            bool secondary;
        };
        MenuItem items[] = {
            {"New Tab", "Ctrl+T", false},
            {"Bookmark This Page", "Ctrl+D", false},
            {"Find in Page", "Ctrl+F", false},
            {"Downloads", "", false},
            {"Settings", "", false},
        };

        f32 iy = dy + pad;
        for (u32 i = 0; i < ChromeUI::MENU_ITEM_COUNT; i++) {
            auto &item = items[i];
            bool hovered = (chrome_.hovered_menu_item == static_cast<i32>(i));
            if (hovered)
                renderer_->fill_rect(dx + 2.0f, iy, mw - 4.0f, item_h, t.accent);
            render::Color tc = hovered ? render::Color::WHITE : (item.secondary ? t.text_secondary : t.text);
            text_renderer_->render_text(renderer_.get(), item.label, dx + 12.0f, iy + 6.0f, tc, 13);
            if (item.shortcut[0]) {
                render::Color sc = hovered ? render::Color{1.0f, 1.0f, 1.0f, 0.7f} : t.text_secondary;
                text_renderer_->render_text(renderer_.get(),
                                            item.shortcut,
                                            dx + mw - 12.0f - text_renderer_->measure_text(item.shortcut, 11),
                                            iy + 7.0f,
                                            sc,
                                            11);
            }
            iy += item_h;
        }
    }

    void BrowserWindow::render_bookmarks_dropdown() {
        if (!chrome_.show_bookmarks_dropdown)
            return;
        auto &t = theme_;

        auto all = bookmarks_ ? bookmarks_->all() : std::vector<Bookmark>();
        f32 item_h = 28.0f;
        f32 header_h = 28.0f;
        f32 max_visible_items = 12.0f;
        f32 max_list_h = max_visible_items * item_h;
        f32 list_h = std::max(item_h, static_cast<f32>(all.size()) * item_h);
        f32 visible_list_h = std::min(list_h, max_list_h);
        f32 pad = 4.0f;

        auto dd = bookmarks_dropdown_rect();
        f32 dx = dd.x, dy = dd.y, dw = dd.w, dh = dd.h;

        // Shadow (subtle offset dark rect behind the dropdown)
        renderer_->fill_rect(dx + 3.0f, dy + 3.0f, dw, dh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 1.5f});
        renderer_->fill_rect(dx + 1.5f, dy + 1.5f, dw, dh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 0.8f});

        // Main dropdown background
        renderer_->fill_rect(dx, dy, dw, dh, t.surface);
        renderer_->stroke_rect(dx, dy, dw, dh, t.border, 1.0f);

        // Small arrow/pointer at the top pointing toward the bookmark button
        f32 arrow_cx = chrome_.rects.bookmark.x + chrome_.rects.bookmark.w * 0.5f;
        f32 arrow_cy = dy;
        f32 arrow_sz = 5.0f;
        // Only draw arrow if dropdown is below chrome (not flipped above)
        if (dy > chrome_height()) {
            // Fill a small triangle using lines
            renderer_->draw_line(arrow_cx - arrow_sz, arrow_cy, arrow_cx, arrow_cy - arrow_sz, t.surface, 2.0f);
            renderer_->draw_line(arrow_cx, arrow_cy - arrow_sz, arrow_cx + arrow_sz, arrow_cy, t.surface, 2.0f);
            // Cover the border line behind the arrow
            renderer_->draw_line(arrow_cx - arrow_sz, arrow_cy, arrow_cx + arrow_sz, arrow_cy, t.surface, 2.0f);
        }

        // Header: "Bookmarks" title
        f32 iy = dy + pad;
        renderer_->fill_rect(dx + 1.0f, iy, dw - 2.0f, header_h - pad, t.surface_hover);
        text_renderer_->render_text(renderer_.get(), "Bookmarks", dx + 10.0f, iy + 4.0f, t.text, 13);
        // Header separator
        renderer_->draw_line(dx + 4.0f, iy + header_h - pad, dx + dw - 4.0f, iy + header_h - pad, t.border, 1.0f);

        iy += header_h;

        if (all.empty()) {
            text_renderer_->render_text(
                renderer_.get(), "No bookmarks yet", dx + 10.0f, iy + 4.0f, t.text_secondary, 12);
            return;
        }

        // Calculate scroll bounds
        f32 total_list_h = static_cast<f32>(all.size()) * item_h;
        f32 max_scroll = std::max(0.0f, total_list_h - visible_list_h);
        if (chrome_.bookmark_scroll_offset > max_scroll)
            chrome_.bookmark_scroll_offset = max_scroll;
        if (chrome_.bookmark_scroll_offset < 0.0f)
            chrome_.bookmark_scroll_offset = 0.0f;

        f32 scroll_off = chrome_.bookmark_scroll_offset;
        f32 first_visible = std::floor(scroll_off / item_h);
        f32 y_start = iy - std::fmod(scroll_off, item_h);

        i32 start_idx = static_cast<i32>(first_visible);
        if (start_idx < 0)
            start_idx = 0;
        i32 end_idx = start_idx + static_cast<i32>(max_visible_items) + 1;
        if (end_idx > static_cast<i32>(all.size()))
            end_idx = static_cast<i32>(all.size());

        for (i32 i = start_idx; i < end_idx; i++) {
            auto &b = all[static_cast<size_t>(i)];
            f32 item_y = y_start + (i - start_idx) * item_h;

            // Clip to visible area
            if (item_y + item_h < iy || item_y > iy + visible_list_h)
                continue;

            // Hover highlight
            if (chrome_.hovered_bookmark_item == i) {
                renderer_->fill_rect(dx + 2.0f, item_y, dw - 4.0f, item_h, t.accent);
            }

            // Bookmark icon (small star)
            renderer_->draw_icon(render::Icon::BOOKMARK_ON,
                                 dx + 8.0f,
                                 item_y + 6.0f,
                                 12.0f,
                                 chrome_.hovered_bookmark_item == i ? render::Color::WHITE : t.bookmark_fill);

            // Title (primary line)
            std::string label = b.title.empty() ? b.url : b.title;
            f32 max_label_w = dw - 48.0f;
            if (text_renderer_->measure_text(label, 12) > max_label_w) {
                while (!label.empty() && text_renderer_->measure_text(label + "..", 12) > max_label_w) label.pop_back();
                label += "..";
            }
            render::Color title_color = chrome_.hovered_bookmark_item == i ? render::Color::WHITE : t.text;
            text_renderer_->render_text(renderer_.get(), label, dx + 26.0f, item_y + 2.0f, title_color, 12);

            // URL (secondary line, smaller)
            std::string url_label = b.url;
            if (text_renderer_->measure_text(url_label, 10) > max_label_w) {
                while (!url_label.empty() && text_renderer_->measure_text(url_label + "..", 10) > max_label_w)
                    url_label.pop_back();
                url_label += "..";
            }
            render::Color url_color =
                chrome_.hovered_bookmark_item == i ? render::Color{1.0f, 1.0f, 1.0f, 0.8f} : t.text_secondary;
            text_renderer_->render_text(renderer_.get(), url_label, dx + 26.0f, item_y + 15.0f, url_color, 10);

            // Subtle separator between items
            if (i < static_cast<i32>(all.size()) - 1) {
                renderer_->draw_line(
                    dx + 26.0f, item_y + item_h - 0.5f, dx + dw - 8.0f, item_y + item_h - 0.5f, t.border, 0.5f);
            }

            // Delete "×" on the hovered row's right edge
            if (chrome_.hovered_bookmark_item == static_cast<i32>(i)) {
                render::Color xc = (chrome_.hovered_bookmark_delete == static_cast<i32>(i))
                                       ? render::Color{0.9f, 0.3f, 0.3f, 1.0f}
                                       : t.text_secondary;
                text_renderer_->render_text(renderer_.get(), "\u00D7", dx + dw - 20.0f, item_y + 6.0f, xc, 13);
            }
        }

        // Scrollbar indicator (if needed)
        if (max_scroll > 0.0f) {
            f32 sb_x = dx + dw - 5.0f;
            f32 sb_y = iy;
            f32 sb_h = visible_list_h;
            f32 thumb_h = std::max(20.0f, sb_h * visible_list_h / total_list_h);
            f32 thumb_y = sb_y + (sb_h - thumb_h) * (scroll_off / max_scroll);
            renderer_->fill_rect(sb_x, sb_y, 3.0f, sb_h, {0.0f, 0.0f, 0.0f, 0.06f});
            renderer_->fill_rect(sb_x, thumb_y, 3.0f, thumb_h, {0.0f, 0.0f, 0.0f, 0.2f});
        }
    }

    void BrowserWindow::render_context_menu() {
        if (!chrome_.show_context_menu)
            return;
        auto &t = theme_;
        auto r = context_menu_rect();
        const f32 dx = r.x, dy = r.y, dw = r.w, dh = r.h;
        constexpr f32 item_h = ChromeUI::CTX_ITEM_H;
        constexpr f32 pad = 4.0f;

        renderer_->fill_rect(dx + 3.0f, dy + 3.0f, dw, dh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 1.5f});
        renderer_->fill_rect(dx + 1.5f, dy + 1.5f, dw, dh, {0.0f, 0.0f, 0.0f, t.shadow_alpha * 0.8f});
        renderer_->fill_rect(dx, dy, dw, dh, t.surface);
        renderer_->stroke_rect(dx, dy, dw, dh, t.border, 1.0f);

        // On a link: link actions. Elsewhere: navigation actions.
        const char *labels[3];
        u32 count;
        if (chrome_.context_on_link) {
            labels[0] = "Open Link in New Tab";
            labels[1] = "Copy Link Address";
            count = 2;
        } else {
            labels[0] = "Back";
            labels[1] = "Forward";
            labels[2] = "Reload";
            count = 3;
        }

        f32 iy = dy + pad;
        for (u32 i = 0; i < count; i++) {
            bool hovered = (chrome_.hovered_context_item == static_cast<i32>(i));
            if (hovered)
                renderer_->fill_rect(dx + 2.0f, iy, dw - 4.0f, item_h, t.accent);
            render::Color tc = hovered ? render::Color::WHITE : t.text;
            text_renderer_->render_text(renderer_.get(), labels[i], dx + 12.0f, iy + 6.0f, tc, 13);
            iy += item_h;
        }
    }

}  // namespace browser
