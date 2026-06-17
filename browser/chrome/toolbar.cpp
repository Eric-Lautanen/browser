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

        std::string text = chrome_.address_focused ? chrome_.edit_buffer : chrome_.url;
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

        if (chrome_.address_focused && chrome_.all_selected && !text.empty()) {
            f32 text_w = text_renderer_->measure_text(text, 13);
            renderer_->fill_rect(tx, ty, text_w, 16, {0.2f, 0.4f, 0.9f, 0.25f});
        }

        text_renderer_->render_text(renderer_.get(), text, tx, ty, t.text, 13);

        if (chrome_.address_focused) {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            u64 elapsed = ms - chrome_.blink_start_ms;
            if ((elapsed / 500) % 2 == 0) {
                f32 cx = tx + text_renderer_->measure_text(chrome_.edit_buffer.substr(0, chrome_.cursor_pos), 13);
                renderer_->draw_line(cx, ty, cx, ty + 16, t.text, 1.5f);
            }
        }
    }

    void BrowserWindow::render_download_button() {
        auto &t = theme_;
        auto &r = chrome_.rects.download;

        // Only visible when downloads are active or mode is enabled
        if (download_manager_->behavior() != DownloadBehavior::ENABLED) {
            if (download_manager_->items().empty())
                return;
        }
        if (download_manager_->items().empty())
            return;

        if (chrome_.hovered_button == ChromeUI::DOWNLOAD)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        renderer_->draw_icon_centered(render::Icon::MENU, r.x, r.y, r.w, r.h, 14.0f, t.text);
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
        auto &t = theme_;
        auto &mr = chrome_.rects.menu;
        f32 mx = mr.x - 80, my = chrome_height();
        f32 mw = 160, mh = 70;

        renderer_->fill_rect(mx, my, mw, mh, t.surface);
        renderer_->stroke_rect(mx, my, mw, mh, t.border, 1.0f);
        text_renderer_->render_text(renderer_.get(), "New Tab    Ctrl+T", mx + 10, my + 6, t.text, 13);
        text_renderer_->render_text(renderer_.get(), "Settings", mx + 10, my + 28, t.text, 13);
        text_renderer_->render_text(renderer_.get(), "About", mx + 10, my + 50, t.text_secondary, 11);
    }

    void BrowserWindow::render_bookmarks_dropdown() {
        if (!chrome_.show_bookmarks_dropdown)
            return;
        auto &t = theme_;
        auto &br = chrome_.rects.bookmark;

        f32 dx = br.x;
        f32 dy = chrome_height();
        auto all = bookmarks_ ? bookmarks_->all() : std::vector<Bookmark>();
        f32 item_h = 24.0f;
        f32 dw = 280.0f;
        f32 dh = std::max(item_h * 2.0f, static_cast<f32>(all.size()) * item_h + item_h);
        dh = std::min(dh, 400.0f);

        renderer_->fill_rect(dx, dy, dw, dh, t.surface);
        renderer_->stroke_rect(dx, dy, dw, dh, t.border, 1.0f);

        f32 iy = dy + 4;
        if (all.empty()) {
            text_renderer_->render_text(renderer_.get(), "No bookmarks yet", dx + 10, iy, t.text_secondary, 12);
            return;
        }
        for (auto &b : all) {
            // Truncate long titles
            std::string label = b.title.empty() ? b.url : b.title;
            if (text_renderer_->measure_text(label, 12) > dw - 20) {
                while (!label.empty() && text_renderer_->measure_text(label + "..", 12) > dw - 20) label.pop_back();
                label += "..";
            }
            text_renderer_->render_text(renderer_.get(), label, dx + 8, iy, t.text, 12);
            // Draw separator
            if (&b != &all.back())
                renderer_->draw_line(dx + 4, iy + item_h - 1, dx + dw - 4, iy + item_h - 1, t.border, 0.5f);
            iy += item_h;
        }
    }

}  // namespace browser
