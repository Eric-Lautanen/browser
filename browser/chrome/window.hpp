#pragma once
#include "../../css/layout.hpp"
#include "../../platform/window.hpp"
#include "../../render/font/atlas.hpp"
#include "../../render/renderer.hpp"
#include "../../render/texture.hpp"
#include "../devtools.hpp"
#include "../download_manager.hpp"
#include "../find_bar.hpp"
#include "../page_loader.hpp"
#include "../session.hpp"
#include "../theme.hpp"

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
        std::string edit_buffer;
        u32 cursor_pos = 0;
        u32 sel_start = 0;
        bool all_selected = false;
        i32 scroll_y = 0;
        i32 scroll_max = 0;
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
        i32 hovered_menu_item = -1;
        f32 bookmark_scroll_offset = 0.0f;
        bool show_settings = false;
        bool ctrl_down = false, shift_down = false, alt_down = false;

        bool fullscreen = false;
        bool show_downloads = false;
        FindState find_state;
        DevToolsState devtools;

        static constexpr f32 TITLEBAR_H = 32.0f;
        static constexpr f32 TOOLBAR_H = 36.0f;
        static constexpr f32 CHROME_H = TITLEBAR_H + TOOLBAR_H;
        static f32 effective_chrome_h(bool fullscreen) { return fullscreen ? 0.0f : CHROME_H; }
        static constexpr f32 BTN_SIZE = 28.0f;
        static constexpr f32 TAB_W = 32.0f;
        static constexpr f32 NEW_TAB_W = 24.0f;
        static constexpr f32 PADDING = 6.0f;
        static constexpr f32 TAB_FAVICON_SIZE = 16.0f;
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

    private:
        std::unique_ptr<platform::Window> window_;
        std::unique_ptr<render::Renderer> renderer_;
        std::unique_ptr<render::TextRenderer> text_renderer_;
        std::unique_ptr<render::FontManager> fm_;
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

        void compute_layout();
        void render_chrome();
        void render_page();
        void render_scrollbar();
        void render_tabs();
        void render_caption_buttons();
        void render_nav_buttons();
        void render_address_bar();
        void render_bookmark();
        void render_bookmarks_dropdown();
        void render_menu_button();
        void render_menu();
        void render_settings();
        void render_download_button();
        void render_download_panel();
        void render_find_bar();
        void render_devtools();
        void handle_event(const platform::Event &e);
        void handle_mouse_click(i32 x, i32 y);
        void handle_key_down(const platform::Event &e);
        void handle_mouse_move(i32 x, i32 y);
        void handle_scroll(i32 delta);
        void start_load(const std::string &url);
        void set_theme(ThemeMode mode);
        void new_tab(const std::string &url = "about:blank");
        void close_tab(u32 index);
        void update_tab_placeholder(u32 index);
        void update_chrome_state();
        void handle_bookmark_click();
        LRESULT hit_test_titlebar(i32 x, i32 y);
        void update_tab_tooltip(i32 mx, i32 my);

        void check_resize();
        void do_relayout();

        static f32 text_measure_cb(void *ctx, const std::string &text, u32 pixel_size);
        static css::FontMetrics text_metrics_cb(void *ctx, u32 pixel_size);

        bool resize_pending_ = false;
        std::chrono::steady_clock::time_point resize_last_time_;

        HWND tooltip_hwnd_ = nullptr;
        std::string tooltip_text_;
        SelectionState selection_;
    };

}  // namespace browser
