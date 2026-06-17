#include "../../render/icons.hpp"
#include "window.hpp"

#include <commctrl.h>
#include <windows.h>

namespace browser {

    void BrowserWindow::render_tabs() {
        auto &t = theme_;
        f32 tab_y = (ChromeUI::TITLEBAR_H - ChromeUI::BTN_SIZE) / 2.0f;

        for (u32 i = 0; i < chrome_.tabs.size(); i++) {
            f32 tx = ChromeUI::PADDING + i * (ChromeUI::TAB_W + 2);

            auto bg = (i == chrome_.active_tab) ? t.tab_active : t.tab_inactive;
            if (chrome_.hovered_tab == static_cast<i32>(i) && i != chrome_.active_tab)
                bg = t.surface_hover;
            renderer_->fill_rect(tx, tab_y, ChromeUI::TAB_W, ChromeUI::BTN_SIZE, bg);

            f32 fx = tx + (ChromeUI::TAB_W - ChromeUI::TAB_FAVICON_SIZE) / 2;
            f32 fy = tab_y + (ChromeUI::BTN_SIZE - ChromeUI::TAB_FAVICON_SIZE) / 2;
            if (chrome_.tabs[i].favicon) {
                renderer_->fill_rect_tex(
                    fx, fy, ChromeUI::TAB_FAVICON_SIZE, ChromeUI::TAB_FAVICON_SIZE, t.text, chrome_.tabs[i].favicon);
            } else {
                renderer_->fill_rect(
                    fx, fy, ChromeUI::TAB_FAVICON_SIZE, ChromeUI::TAB_FAVICON_SIZE, chrome_.tabs[i].placeholder_color);
            }

            if (chrome_.hovered_tab == static_cast<i32>(i) || chrome_.hovered_close == static_cast<i32>(i)) {
                auto &close_r = chrome_.rects.tab_close[i];
                if (chrome_.hovered_close == static_cast<i32>(i))
                    renderer_->fill_rect(close_r.x, close_r.y, close_r.w, close_r.h, t.surface_hover);
                text_renderer_->render_text(
                    renderer_.get(), "\u00D7", close_r.x + 2, close_r.y + 1, t.text_secondary, 12);
            }
        }

        f32 nx = ChromeUI::PADDING + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
        if (chrome_.hovered_button == ChromeUI::REFRESH + 1)
            renderer_->fill_rect(nx, tab_y, ChromeUI::NEW_TAB_W, ChromeUI::BTN_SIZE, t.surface_hover);
        text_renderer_->render_text(renderer_.get(), "+", nx + 5, tab_y + 3, t.text, 16);
    }

    void BrowserWindow::render_caption_buttons() {
        using render::Icon;
        auto &t = theme_;
        constexpr f32 ICN = 12.0f;

        auto draw_cap =
            [&](const ChromeUI::ButtonRect &r, i32 id, Icon icon, render::Color hover_color, render::Color icon_color) {
                if (chrome_.hovered_button == id)
                    renderer_->fill_rect(r.x, r.y, r.w, r.h, hover_color);
                renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, ICN, icon_color);
            };

        draw_cap(chrome_.rects.minimize_btn, ChromeUI::MINIMIZE, Icon::MINIMIZE, t.surface_hover, t.text_secondary);
        draw_cap(chrome_.rects.maximize_btn, ChromeUI::MAXIMIZE, Icon::MAXIMIZE, t.surface_hover, t.text_secondary);
        draw_cap(chrome_.rects.close_btn, ChromeUI::CLOSE, Icon::CLOSE, {0.9f, 0.2f, 0.2f, 1.0f}, t.text_secondary);
    }

    void BrowserWindow::update_tab_tooltip(i32 mx, i32 my) {
        HWND hwnd = static_cast<HWND>(window_->get_native_handle());
        TOOLINFO ti = {};
        ti.cbSize = sizeof(TOOLINFO);
        ti.uFlags = TTF_TRACK | TTF_IDISHWND;
        ti.hwnd = hwnd;
        ti.uId = reinterpret_cast<UINT_PTR>(hwnd);

        i32 tab_idx = -1;
        if (my >= 0 && my < (i32)chrome_height()) {
            for (u32 i = 0; i < chrome_.rects.tab_close.size(); i++) {
                if (is_in_rect(mx, my, chrome_.rects.tab_close[i])) {
                    tab_idx = (i32)i;
                    break;
                }
            }
            if (tab_idx < 0) {
                f32 tab_start = ChromeUI::PADDING;
                for (u32 i = 0; i < chrome_.tabs.size(); i++) {
                    if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
                        mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
                        tab_idx = (i32)i;
                        break;
                    }
                }
            }
        }

        if (tab_idx >= 0 && tab_idx < (i32)chrome_.tabs.size()) {
            tooltip_text_ = chrome_.tabs[tab_idx].url;
            if (tooltip_text_.length() > 60) {
                tooltip_text_ = tooltip_text_.substr(0, 57) + "...";
            }
            ti.lpszText = const_cast<char *>(tooltip_text_.c_str());
            SendMessage(tooltip_hwnd_, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&ti));
            SendMessage(tooltip_hwnd_, TTM_TRACKPOSITION, 0, MAKELPARAM(mx, my + 24));
            SendMessage(tooltip_hwnd_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
        } else {
            SendMessage(tooltip_hwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
        }
    }

}  // namespace browser
