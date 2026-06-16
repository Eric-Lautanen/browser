#include "../../render/icons.hpp"
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

        std::string text = chrome_.address_focused ? chrome_.edit_buffer : chrome_.url;
        f32 tx = r.x + 6;
        f32 ty = r.y + 4;

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

    void BrowserWindow::render_bookmark() {
        using render::Icon;
        auto &t = theme_;
        auto &r = chrome_.rects.bookmark;
        if (chrome_.hovered_button == ChromeUI::BOOKMARK)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        Icon icon = chrome_.is_bookmarked ? Icon::BOOKMARK_ON : Icon::BOOKMARK_OFF;
        render::Color c = chrome_.is_bookmarked ? t.bookmark_fill : t.text_secondary;
        renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, 14.0f, c);
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
        f32 mx = mr.x - 80, my = ChromeUI::CHROME_H;
        f32 mw = 160, mh = 70;

        renderer_->fill_rect(mx, my, mw, mh, t.surface);
        renderer_->stroke_rect(mx, my, mw, mh, t.border, 1.0f);
        text_renderer_->render_text(renderer_.get(), "New Tab    Ctrl+T", mx + 10, my + 6, t.text, 13);
        text_renderer_->render_text(renderer_.get(), "Settings", mx + 10, my + 28, t.text, 13);
        text_renderer_->render_text(renderer_.get(), "About", mx + 10, my + 50, t.text_secondary, 11);
    }

}  // namespace browser
