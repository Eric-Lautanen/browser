#include "window.hpp"

#include "../../net/http_client.hpp"
#include "../../net/tracker_blocker.hpp"
#include "../../net/url.hpp"
#include "../../platform/opengl.hpp"
#include "../../platform/window_win32.hpp"
#include "../bookmarks.hpp"
#include "../history.hpp"
#include "../settings.hpp"
#include "../telemetry.hpp"

#include <commctrl.h>
#include <windows.h>

namespace browser {

    BrowserWindow::BrowserWindow() = default;
    BrowserWindow::~BrowserWindow() = default;

    Result<void> BrowserWindow::initialize() {
        auto win = platform::Window::create_window("Browser", 1024, 768);
        if (win.is_err())
            return win.unwrap_err();
        window_ = std::move(win.unwrap());
        window_->make_context_current();
        browser::platform::load_opengl_functions();

        tracker_ = std::make_unique<net::TrackerBlocker>();
        tracker_->load_default_list();
        net::HTTPClient::set_tracker_blocker(tracker_.get());

        telemetry_ = std::make_unique<Telemetry>();
        settings_ = std::make_unique<SettingsManager>();
        auto rs = settings_->load_from_file("./settings.txt");

        set_theme(settings_->theme());

        renderer_ = std::make_unique<render::Renderer>();
        auto r = renderer_->initialize(1024, 768);
        if (r.is_err())
            return r.unwrap_err();

        text_renderer_ = std::make_unique<render::TextRenderer>();
        fm_ = std::make_unique<render::FontManager>();

        bool font_ok = false;
        const char *font_paths[] = {"C:\\Windows\\Fonts\\arial.ttf",
                                    "C:\\Windows\\Fonts\\consola.ttf",
                                    "C:\\Windows\\Fonts\\cour.ttf",
                                    "C:\\Windows\\Fonts\\lucon.ttf"};
        for (auto p : font_paths) {
            auto r = fm_->load_from_file(p);
            if (r.is_ok()) {
                text_renderer_->initialize(r.unwrap(), fm_.get());
                font_ok = true;
                {
                    static FILE *f = nullptr;
                    if (!f) {
                        f = fopen("font_debug.txt", "w");
                    }
                    if (f) {
                        std::string msg = "loaded: " + std::string(p) + "\n";
                        fprintf(f, "%s", msg.c_str());
                        fflush(f);
                    }
                }
                break;
            } else {
                {
                    static FILE *f = nullptr;
                    if (!f) {
                        f = fopen("font_debug.txt", "w");
                    }
                    if (f) {
                        std::string msg = "FAILED: " + std::string(p) + " - " + r.unwrap_err() + "\n";
                        fprintf(f, "%s", msg.c_str());
                        fflush(f);
                    }
                }
            }
        }
        if (!font_ok) {
            auto font_r = fm_->load_default_font();
            if (font_r.is_ok()) {
                text_renderer_->initialize(font_r.unwrap(), fm_.get());
                font_ok = true;
                {
                    static FILE *f = fopen("font_debug.txt", "a");
                    if (f) {
                        fprintf(f, "using embedded default font\n");
                        fflush(f);
                        fclose(f);
                    }
                }
            } else {
                text_renderer_->initialize(fm_.get());
                {
                    static FILE *f = fopen("font_debug.txt", "a");
                    if (f) {
                        fprintf(f, "using FontManager fallback\n");
                        fflush(f);
                        fclose(f);
                    }
                }
            }
        }

        page_loader_ = std::make_unique<PageLoader>(
            telemetry_.get(), settings_.get(), tracker_.get(), fm_.get(), text_renderer_.get());
        page_loader_->set_viewport_size(viewport_width_, viewport_height_);

        compositor_ = std::make_unique<render::Compositor>();
        compositor_->set_viewport(static_cast<i32>(viewport_width_), static_cast<i32>(viewport_height_));
        compositor_->start();
        composited_texture_ = std::make_unique<render::Texture2D>();

        history_ = std::make_unique<HistoryManager>();
        bookmarks_ = std::make_unique<BookmarkManager>();
        auto rb = bookmarks_->load_from_file("./bookmarks.txt");

        window_->set_event_callback([this](const platform::Event &e) { handle_event(e); });

        {
            auto *w32 = static_cast<platform::Win32Window *>(window_.get());
            w32->nchittest_callback_ = [this](i32 x, i32 y) -> LRESULT { return hit_test_titlebar(x, y); };
        }

        {
            HWND hwnd = static_cast<HWND>(window_->get_native_handle());
            INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
            InitCommonControlsEx(&icex);
            tooltip_hwnd_ = CreateWindowEx(0,
                                           TOOLTIPS_CLASS,
                                           nullptr,
                                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           hwnd,
                                           nullptr,
                                           GetModuleHandle(nullptr),
                                           nullptr);
            TOOLINFO ti = {};
            ti.cbSize = sizeof(TOOLINFO);
            ti.uFlags = TTF_TRACK | TTF_IDISHWND;
            ti.hwnd = hwnd;
            ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
            ti.lpszText = const_cast<LPSTR>("");
            SendMessage(tooltip_hwnd_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
        }

        {
            auto ext = window_->get_extent();
            viewport_width_ = ext.width;
            viewport_height_ = ext.height;
        }
        {
            TabInfo tab;
            tab.url = "about:blank";
            chrome_.tabs.push_back(tab);
            chrome_.active_tab = 0;
            chrome_.url = "about:blank";
        }
        compute_layout();
        return {};
    }

    void BrowserWindow::run() {
        window_->show();

        // Initial render
        {
            renderer_->begin_frame();
            renderer_->set_viewport(viewport_width_, viewport_height_);
            update_chrome_state();
            render_chrome();
            renderer_->end_textured();
            render_page();
            if (chrome_.show_settings) {
                render_settings();
            }
            renderer_->end_frame();
            window_->swap_buffers();
        }

        // Event loop: wait on compositor frame and window messages
        HANDLE wait_handles[] = {compositor_->frame_ready_event()};

        while (!window_->should_close()) {
            // 1. Wait for any signal (messages, compositor frame, timers)
            DWORD wait = MsgWaitForMultipleObjectsEx(
                ARRAYSIZE(wait_handles), wait_handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            (void)wait;

            // 2. Process pending Windows messages (non-blocking drain)
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (window_->should_close())
                break;

            // 3-5. Stubbed: timers, microtasks, rAF

            // 6. Render frame
            renderer_->begin_frame();
            renderer_->set_viewport(viewport_width_, viewport_height_);
            update_chrome_state();
            render_chrome();
            renderer_->end_textured();

            // render_page() handles both compositor and old paths internally
            render_page();
            if (chrome_.show_settings) {
                render_settings();
            }
            renderer_->end_frame();
            window_->swap_buffers();
        }
    }

    void BrowserWindow::compute_layout() {
        f32 right = static_cast<f32>(viewport_width_);

        f32 tab_y = (ChromeUI::TITLEBAR_H - ChromeUI::BTN_SIZE) / 2.0f;
        f32 cx = ChromeUI::PADDING;

        chrome_.rects.tab_close.resize(chrome_.tabs.size());
        for (u32 i = 0; i < chrome_.tabs.size(); i++) {
            f32 tx = cx + i * (ChromeUI::TAB_W + 2);
            chrome_.rects.tab_close[i] = {tx + ChromeUI::TAB_W - 14, tab_y + 2, 12, 12};
        }
        f32 tabs_end = cx + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
        chrome_.rects.new_tab = {tabs_end, tab_y, ChromeUI::NEW_TAB_W, ChromeUI::BTN_SIZE};

        const f32 CAP_W = 46.0f, CAP_H = ChromeUI::TITLEBAR_H;
        chrome_.rects.close_btn = {right - CAP_W, 0, CAP_W, CAP_H};
        chrome_.rects.maximize_btn = {right - CAP_W * 2, 0, CAP_W, CAP_H};
        chrome_.rects.minimize_btn = {right - CAP_W * 3, 0, CAP_W, CAP_H};

        f32 btn_y = ChromeUI::TITLEBAR_H + (ChromeUI::TOOLBAR_H - ChromeUI::BTN_SIZE) / 2.0f;
        f32 nav_x = ChromeUI::PADDING;

        chrome_.rects.back = {nav_x, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        chrome_.rects.forward = {nav_x + ChromeUI::BTN_SIZE + 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        chrome_.rects.refresh = {nav_x + (ChromeUI::BTN_SIZE + 2) * 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        f32 nav_end = nav_x + (ChromeUI::BTN_SIZE + 2) * 3 + ChromeUI::PADDING;

        f32 right2 = right - ChromeUI::PADDING;
        chrome_.rects.menu = {right2 - ChromeUI::BTN_SIZE, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        chrome_.rects.bookmark = {right2 - ChromeUI::BTN_SIZE * 2 - 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        chrome_.rects.address = {nav_end, btn_y, chrome_.rects.bookmark.x - 4 - nav_end, ChromeUI::BTN_SIZE};
    }

    void BrowserWindow::render_chrome() {
        auto &t = theme_;
        bool active = window_->is_active();

        auto title_bg = active ? t.bg : render::Color{t.bg.r * 0.95f, t.bg.g * 0.95f, t.bg.b * 0.95f, 1.0f};
        renderer_->fill_rect(0, 0, (f32)viewport_width_, ChromeUI::TITLEBAR_H, title_bg);

        render_tabs();
        render_caption_buttons();

        renderer_->fill_rect(0, ChromeUI::TITLEBAR_H, (f32)viewport_width_, ChromeUI::TOOLBAR_H, t.bg);
        renderer_->draw_line(0, ChromeUI::CHROME_H, (f32)viewport_width_, ChromeUI::CHROME_H, t.border, 1.0f);
        for (f32 i = 1; i < 3; i++) {
            renderer_->draw_line(0,
                                 ChromeUI::CHROME_H + i,
                                 (f32)viewport_width_,
                                 ChromeUI::CHROME_H + i,
                                 {0.0f, 0.0f, 0.0f, t.shadow_alpha * (0.4f - i * 0.12f)});
        }

        render_nav_buttons();
        render_address_bar();
        render_bookmark();
        render_menu_button();
        if (chrome_.show_menu)
            render_menu();
    }

    void BrowserWindow::set_theme(ThemeMode mode) {
        Theme::current = mode;
        theme_ = (mode == ThemeMode::LIGHT) ? Theme::light() : Theme::dark();
    }

    void BrowserWindow::update_chrome_state() {
        if (history_) {
            chrome_.can_go_back = history_->can_go_back();
            chrome_.can_go_forward = history_->can_go_forward();
        }
        if (bookmarks_) {
            chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
        }
        if (page_loader_) {
            chrome_.is_loading = page_loader_->is_loading();
        }
    }

    void BrowserWindow::update_tab_placeholder(u32 index) {
        auto &tab = chrome_.tabs[index];
        auto url = net::URL::parse(tab.url);
        if (url.is_err())
            return;
        auto host = url.unwrap().host;
        u32 hash = 0;
        for (char c : host) hash = hash * 31 + static_cast<u8>(c);
        tab.placeholder_color = {0.5f + static_cast<f32>((hash >> 16) & 0xFF) / 510.0f,
                                 0.5f + static_cast<f32>((hash >> 8) & 0xFF) / 510.0f,
                                 0.5f + static_cast<f32>(hash & 0xFF) / 510.0f,
                                 1.0f};
    }

    void BrowserWindow::start_load(const std::string &url) {
        chrome_.scroll_y = 0;
        if (page_loader_) {
            page_loader_->start_load(url);
        }
        if (telemetry_ && tracker_) {
            telemetry_->set_trackers_blocked(tracker_->blocked_count());
        }
    }

    LRESULT BrowserWindow::hit_test_titlebar(i32 mx, i32 my) {
        const int border = 6;
        RECT wr;
        GetWindowRect(static_cast<HWND>(window_->get_native_handle()), &wr);
        int w = wr.right - wr.left;

        if (my < border) {
            if (mx < border)
                return HTTOPLEFT;
            if (mx > w - border)
                return HTTOPRIGHT;
            return HTTOP;
        }
        if (my >= (i32)ChromeUI::TITLEBAR_H)
            return HTCLIENT;

        auto &r = chrome_.rects;
        if (is_in_rect(mx, my, r.close_btn) || is_in_rect(mx, my, r.maximize_btn) || is_in_rect(mx, my, r.minimize_btn))
            return HTCLIENT;

        f32 tab_start = ChromeUI::PADDING;
        for (u32 i = 0; i < chrome_.tabs.size(); i++) {
            f32 tx = tab_start + i * (ChromeUI::TAB_W + 2);
            if (mx >= tx && mx <= tx + ChromeUI::TAB_W)
                return HTCLIENT;
        }
        f32 ntx = tab_start + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
        if (mx >= ntx && mx <= ntx + ChromeUI::NEW_TAB_W)
            return HTCLIENT;

        return HTCAPTION;
    }

    bool BrowserWindow::is_in_rect(i32 x, i32 y, const ChromeUI::ButtonRect &r) {
        return static_cast<f32>(x) >= r.x && static_cast<f32>(x) <= r.x + r.w && static_cast<f32>(y) >= r.y &&
               static_cast<f32>(y) <= r.y + r.h;
    }

}  // namespace browser
