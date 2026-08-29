#pragma once
#include "../../css/animation.hpp"
#include "../../css/layout.hpp"
#include "../../platform/window.hpp"
#include "../../render/font/atlas.hpp"
#include "../../render/paint_executor.hpp"
#include "../../render/renderer.hpp"
#include "../../render/texture.hpp"
#include "../devtools.hpp"
#include "../download_manager.hpp"
#include "../find_bar.hpp"
#include "../history.hpp"
#include "../page_loader.hpp"
#include "../session.hpp"
#include "../theme.hpp"
#include "address_bar.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace browser {
    class HistoryManager;
    class BookmarkManager;
    class Telemetry;
    class SettingsManager;
    class DownloadManager;
    class SessionManager;
    namespace net {
        class TrackerBlocker;
    }

    struct SelectionState {
        bool active = false;
        const css::LayoutNode *start_node = nullptr;
        const css::LayoutNode *end_node = nullptr;
        u32 start_offset = 0;  // character offset within text
        u32 end_offset = 0;
        f32 start_x = 0, start_y = 0;
        f32 end_x = 0, end_y = 0;
        bool dragging = false;

        // The collected text is stored here at select-time so that Ctrl+C
        // always has valid data even if the layout tree is rebuilt.
        std::string selected_text;

        bool has_selection() const { return active && start_node && end_node; }
        bool covers_all_text() const { return active && all_text; }
        bool all_text = false;
        void clear() {
            active = false;
            start_node = end_node = nullptr;
            start_offset = end_offset = 0;
            dragging = false;
            all_text = false;
            selected_text.clear();
        }
    };

    struct TabInfo {
        std::string url;
        render::Texture2D *favicon = nullptr;
        render::Color placeholder_color{0.7f, 0.7f, 0.7f, 1.0f};
        i32 scroll_y = 0;
        std::unique_ptr<HistoryManager> history;
    };

    struct ChromeUI {
        bool address_focused = false;
        struct ButtonRect {
            f32 x, y, w, h;
        };
        struct Rects {
            ButtonRect back, forward, refresh, address, bookmark, bookmark_chevron, menu, download;
            std::vector<ButtonRect> tab_close;
            ButtonRect new_tab;
            ButtonRect scrollbar;
            ButtonRect minimize_btn, maximize_btn, close_btn;
        } rects;
        std::vector<TabInfo> tabs;
        u32 active_tab = 0;
        i32 hovered_tab = -1;
        i32 hovered_close = -1;
        std::string url = "";
        bool can_go_back = false, can_go_forward = false, is_loading = false;
        bool is_bookmarked = false;
        bool is_https = false;
        bool has_mixed_content = false;
        AddressBarEditor address_bar;
        i32 scroll_y = 0;
        i32 scroll_max = 0;
        i32 scroll_target_y = -1;  // -1 = no pending smooth scroll
        bool scroll_dragging = false;
        i32 scroll_drag_start_y = 0;
        i32 scroll_drag_start_pos = 0;
        u64 blink_start_ms = 0;
        i32 hovered_button = -1;
        enum ButtonId {
            BACK = 0,
            FORWARD = 1,
            REFRESH = 2,
            BOOKMARK = 3,
            MENU = 4,
            DOWNLOAD = 5,
            BOOKMARK_CHEVRON = 6,
            MINIMIZE = 7,
            MAXIMIZE = 8,
            CLOSE = 9
        };
        bool show_menu = false;
        bool show_bookmarks_dropdown = false;
        i32 hovered_bookmark_item = -1;
        i32 hovered_bookmark_delete = -1;
        i32 hovered_menu_item = -1;
        f32 bookmark_scroll_offset = 0.0f;
        bool show_settings = false;
        bool ctrl_down = false, shift_down = false, alt_down = false;

        bool fullscreen = false;
        bool show_downloads = false;
        FindState find_state;
        DevToolsState devtools;

        // Page context menu (right-click). Built on the same open/close
        // discipline as the other popups so outside clicks dismiss it.
        bool show_context_menu = false;
        bool context_on_link = false;
        std::string context_link_url;
        f32 context_menu_x = 0.0f, context_menu_y = 0.0f;
        i32 hovered_context_item = -1;

        // Address-bar click cadence for double/triple-click selection.
        u64 last_addr_click_ms = 0;
        u32 addr_click_count = 0;
        i32 last_addr_click_x = 0, last_addr_click_y = 0;

        // Tab-strip click cadence: double-click on a tab toggles maximize.
        u64 last_tab_click_ms = 0;
        i32 last_tab_click_idx = -1;

        // Textarea resize drag state
        struct TextareaResizeState {
            bool active = false;
            html::Element *element = nullptr;
            const css::LayoutNode *layout_node = nullptr;
            f32 start_mouse_x = 0;
            f32 start_mouse_y = 0;
            f32 start_width = 0;
            f32 start_height = 0;
        } textarea_resize;

        static constexpr f32 TITLEBAR_H = 32.0f;
        static constexpr f32 TOOLBAR_H = 36.0f;
        static constexpr f32 CHROME_H = TITLEBAR_H + TOOLBAR_H;
        static f32 effective_chrome_h(bool fullscreen) { return fullscreen ? 0.0f : CHROME_H; }
        static constexpr f32 BTN_SIZE = 28.0f;
        // Mainstream-browser tabs: wide enough for favicon + page title +
        // close button. ~180px fits ~5 tabs on a 1024-px window.
        static constexpr f32 TAB_W = 180.0f;
        static constexpr f32 TAB_H = 24.0f;
        static constexpr f32 NEW_TAB_W = 24.0f;
        static constexpr f32 PADDING = 6.0f;
        static constexpr f32 TAB_FAVICON_SIZE = 14.0f;
        static constexpr f32 TAB_TITLE_GAP = 6.0f;
        static constexpr f32 TAB_CLOSE_W = 16.0f;
        static constexpr f32 TAB_CLOSE_H = 16.0f;
        static constexpr f32 MENU_W = 220.0f;
        static constexpr f32 MENU_ITEM_H = 30.0f;
        static constexpr u32 MENU_ITEM_COUNT = 5;
        static constexpr f32 CTX_MENU_W = 200.0f;
        static constexpr f32 CTX_ITEM_H = 28.0f;

        // Single source of truth for the app-menu popup rect. Render,
        // hit-testing and hover tracking all must agree on this geometry.
        struct MenuGeometry {
            f32 x, y, w, h;
        };
        static MenuGeometry menu_geometry(f32 viewport_w, f32 viewport_h, f32 chrome_h, const ButtonRect &menu_btn) {
            const f32 mw = MENU_W;
            const f32 mh = 8.0f + MENU_ITEM_H * static_cast<f32>(MENU_ITEM_COUNT);
            f32 dx = menu_btn.x + menu_btn.w - mw;
            if (dx < 4.0f)
                dx = 4.0f;
            if (dx + mw > viewport_w - 4.0f)
                dx = viewport_w - mw - 4.0f;
            f32 dy = chrome_h + 2.0f;
            if (dy + mh > viewport_h - 8.0f)
                dy = chrome_h - mh - 2.0f;
            return {dx, dy, mw, mh};
        }
    };

    class BrowserWindow {
    public:
        BrowserWindow();
        ~BrowserWindow();
        Result<void> initialize();
        void run();
        void run_with_screenshot(const std::string &path);
        void navigate(const std::string &url);
        void navigate_back();
        void navigate_forward();
        void refresh();

        static bool is_in_rect(i32 x, i32 y, const ChromeUI::ButtonRect &r);
        f32 chrome_height() const { return chrome_.fullscreen ? 0.0f : ChromeUI::CHROME_H; }

        // BR-P2: how long the message wait sleeps before the next iteration.
        // Continuous animation keeps a 16 ms beat, a visible caret polls at
        // blink granularity, a fully idle browser dozes (bounded, because
        // async loads publish from pool threads).
        static u32 idle_wait_ms(bool animating, bool caret_active);

    private:
        std::unique_ptr<platform::Window> window_;
        std::unique_ptr<render::Renderer> renderer_;
        std::unique_ptr<render::TextRenderer> text_renderer_;
        std::unique_ptr<render::FontManager> fm_;
        // BR-P1: one executor for the whole session. Its GPU caches (images,
        // gradients, canvases) persist across frames; only page swaps drop
        // page-owned entries via invalidate_page_caches().
        std::unique_ptr<render::PaintExecutor> paint_executor_;
        std::unique_ptr<PageLoader> page_loader_;
        ChromeUI chrome_;
        Theme theme_;
        u32 viewport_width_ = 1024, viewport_height_ = 768;
        std::optional<LoadedPage> current_page_;
        std::unique_ptr<BookmarkManager> bookmarks_;
        std::unique_ptr<Telemetry> telemetry_;
        std::unique_ptr<SettingsManager> settings_;
        std::unique_ptr<net::TrackerBlocker> tracker_;
        std::unique_ptr<DownloadManager> download_manager_;
        std::unique_ptr<SessionManager> session_;
        css::AnimationEngine animation_engine_;

        void compute_layout();
        void render_chrome();
        void render_page();
        // BR-P2: drains the loader channel + queued navigations every loop
        // iteration (cheap when empty) so completed loads surface even when
        // rendering is gated off. Sets frame_dirty_ on page adoption.
        void absorb_loaded_pages();
        void render_scrollbar();
        void render_tabs();
        void render_caption_buttons();
        void render_nav_buttons();
        void render_address_bar();
        void render_bookmark();
        void render_bookmarks_dropdown();
        void render_menu_button();
        void render_menu();
        void render_context_menu();
        void render_settings();
        void render_download_button();
        void render_download_panel();
        void render_find_bar();
        void render_devtools();
        void handle_event(const platform::Event &e);
        void handle_mouse_click(i32 x, i32 y, platform::MouseButton button);
        void handle_key_down(const platform::Event &e);
        void handle_mouse_move(i32 x, i32 y);
        void handle_scroll(i32 delta);
        void start_load(const std::string &url);
        void set_theme(ThemeMode mode);
        void new_tab(const std::string &url = "about:blank");
        void close_tab(u32 index);
        void select_tab(u32 index);
        // Opens a tab without switching to it or loading it yet — used by
        // middle-click / Ctrl+click on links. The page loads when the tab
        // is first activated (the loader is single-flight, shared by tabs).
        void open_background_tab(const std::string &url);
        void focus_address_bar(bool select_all);
        // Popup lifecycle: any open popup (hamburger menu, bookmarks
        // dropdown, downloads/settings panels, page context menu) is
        // dismissed by the first click that lands outside of it.
        bool popup_open() const;
        void close_popups();
        ChromeUI::ButtonRect context_menu_rect() const;
        ChromeUI::ButtonRect bookmarks_dropdown_rect() const;
        ChromeUI::ButtonRect overlay_panel_rect() const;
        // Tab-strip hit tests. Both are y-bounded to the titlebar tab row —
        // the back/forward/refresh buttons share x ranges with the tabs and
        // must never match toolbar clicks.
        i32 tab_index_at(i32 mx, i32 my) const;
        bool new_tab_button_hit(i32 mx, i32 my) const;
        void update_tab_placeholder(u32 index);
        void update_chrome_state();
        void handle_bookmark_click();
        LRESULT hit_test_titlebar(i32 x, i32 y);
        void update_tab_tooltip(i32 mx, i32 my);

        void check_resize();
        void do_relayout();
        void do_repaint_only();
        void setup_animations();
        void update_animations(f32 dt);
        void apply_animation_values();

        static f32 text_measure_cb(void *ctx, const std::string &text, u32 pixel_size);
        static css::FontMetrics text_metrics_cb(void *ctx, u32 pixel_size);

        bool resize_pending_ = false;
        std::chrono::steady_clock::time_point resize_last_time_;

        // BR-P3: textarea resize drags used to run a full synchronous
        // relayout on EVERY mouse move. Moves only set this flag; the frame
        // loop consumes it once per iteration.
        bool relayout_pending_ = false;

        // BR-P2: idle dirty-gate. The render block runs only when something
        // changed (input, page load, relayout) or is continuously animating.
        bool frame_dirty_ = true;

        HWND tooltip_hwnd_ = nullptr;
        std::string tooltip_text_;
        SelectionState selection_;
        // A previous session was restored into the tab strip; the next
        // startup navigation (the default "about:blank" from main) reloads
        // the restored active tab instead of blanking it.
        bool session_restored_ = false;
    };

}  // namespace browser
