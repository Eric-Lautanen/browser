#include "window.hpp"

#include "../../css/layout.hpp"
#include "../../html/form_state.hpp"
#include "../../net/http_client.hpp"
#include "../../net/storage.hpp"
#include "../../net/tracker_blocker.hpp"
#include "../../net/url.hpp"
#include "../../platform/opengl.hpp"
#include "../../platform/window_win32.hpp"
#include "../../render/canvas.hpp"
#include "../../render/painter.hpp"
#include "../bookmarks.hpp"
#include "../history.hpp"
#include "../perf_counter.hpp"
#include "../settings.hpp"
#include "../telemetry.hpp"

#include <commctrl.h>
#include <psapi.h>
#include <windows.h>

namespace browser {

    // ---------------------------------------------------------------------------
    // Text measurement callbacks (used by LayoutEngine / do_relayout)
    // ---------------------------------------------------------------------------
    /*static*/ f32 BrowserWindow::text_measure_cb(void *ctx, const std::string &text, u32 pixel_size) {
        return static_cast<render::TextRenderer *>(ctx)->measure_text(text, pixel_size);
    }
    /*static*/ css::FontMetrics BrowserWindow::text_metrics_cb(void *ctx, u32 pixel_size) {
        return static_cast<render::TextRenderer *>(ctx)->get_font_metrics(pixel_size);
    }

    BrowserWindow::BrowserWindow() = default;
    BrowserWindow::~BrowserWindow() {
        if (session_) {
            std::vector<SessionEntry> entries;
            for (auto &tab : chrome_.tabs) {
                SessionEntry e;
                e.url = tab.url;
                e.scroll_y = tab.scroll_y;
                entries.push_back(e);
            }
            session_->save(entries);
        }
        net::Storage::flush_all();
    }

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
        auto rs = settings_->load_from_file(data_dir() + "/settings.txt");

        download_manager_ = std::make_unique<DownloadManager>();
        {
            auto db = settings_->download_behavior();
            if (db == "enabled")
                download_manager_->set_behavior(DownloadBehavior::ENABLED);
            else if (db == "notify")
                download_manager_->set_behavior(DownloadBehavior::NOTIFY);
            else
                download_manager_->set_behavior(DownloadBehavior::DISABLED);
        }

        set_theme(settings_->theme());

        renderer_ = std::make_unique<render::Renderer>();
        {
            auto ext = window_->get_extent();
            viewport_width_ = ext.width;
            viewport_height_ = ext.height;
        }
        auto r = renderer_->initialize(viewport_width_, viewport_height_);
        if (r.is_err())
            return r.unwrap_err();

        text_renderer_ = std::make_unique<render::TextRenderer>();
        fm_ = std::make_unique<render::FontManager>();

        render::FontFace *primary_face = nullptr;
        struct FontEntry {
            const char *path;
        };
        FontEntry font_paths[] = {{"C:\\Windows\\Fonts\\arial.ttf"},
                                  {"C:\\Windows\\Fonts\\consola.ttf"},
                                  {"C:\\Windows\\Fonts\\cour.ttf"},
                                  {"C:\\Windows\\Fonts\\lucon.ttf"}};
        for (auto &fe : font_paths) {
            auto r = fm_->load_from_file(fe.path);
            if (r.is_ok()) {
                if (!primary_face)
                    primary_face = r.unwrap();
            }
        }
        if (primary_face) {
            text_renderer_->initialize(primary_face, fm_.get());
        } else {
            auto font_r = fm_->load_default_font();
            if (font_r.is_ok()) {
                text_renderer_->initialize(font_r.unwrap(), fm_.get());
                primary_face = font_r.unwrap();
            } else {
                text_renderer_->initialize(fm_.get());
            }
        }
        fm_->load_fallback_fonts();

        page_loader_ = std::make_unique<PageLoader>(
            telemetry_.get(), settings_.get(), tracker_.get(), fm_.get(), text_renderer_.get());
        render::setup_canvas_bindings();
        page_loader_->set_viewport_size(viewport_width_, viewport_height_);
        page_loader_->set_download_callback(
            [this](const std::string &url, const std::string &cd, const std::string &mt, u64 cl) -> bool {
                if (download_manager_->should_download(url, cd, mt, cl)) {
                    download_manager_->start_download(url).start();
                    return true;
                }
                return false;
            });

        compositor_ = std::make_unique<render::Compositor>();
        compositor_->set_viewport(static_cast<i32>(viewport_width_), static_cast<i32>(viewport_height_));
        compositor_->start();
        composited_texture_ = std::make_unique<render::Texture2D>();

        bookmarks_ = std::make_unique<BookmarkManager>();
        auto rb = bookmarks_->load_from_file(BookmarkManager::default_path());

        session_ = std::make_unique<SessionManager>();
        {
            auto entries = session_->load();
            if (!entries.empty()) {
                // Restore tabs from session
                chrome_.tabs.clear();
                for (auto &entry : entries) {
                    TabInfo tab;
                    tab.url = entry.url;
                    tab.scroll_y = entry.scroll_y;
                    tab.history = std::make_unique<HistoryManager>();
                    chrome_.tabs.push_back(std::move(tab));
                }
                chrome_.active_tab = 0;
                chrome_.url = chrome_.tabs[0].url;
            }
        }

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
        // Only add default tab if session restore didn't provide tabs
        if (chrome_.tabs.empty()) {
            TabInfo tab;
            tab.url = "about:blank";
            tab.history = std::make_unique<HistoryManager>();
            chrome_.tabs.push_back(std::move(tab));
            chrome_.active_tab = 0;
            chrome_.url = "about:blank";
        }
        compute_layout();
        return {};
    }

    void BrowserWindow::run_with_screenshot(const std::string &path) {
        window_->show();

        // Wait for page to load before capturing screenshot
        if (page_loader_) {
            int max_wait = 10000;
            while (page_loader_->is_loading() && max_wait > 0) {
                Sleep(5);
                max_wait -= 5;
                MSG msg;
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }

        LARGE_INTEGER qpc_freq;
        QueryPerformanceFrequency(&qpc_freq);

        LARGE_INTEGER frame_start;
        QueryPerformanceCounter(&frame_start);

        // Render one frame (same initial render as run())
        int sw = static_cast<int>(viewport_width_);
        int sh = static_cast<int>(viewport_height_);
        std::vector<u8> pixels(static_cast<size_t>(sw * sh * 4));
        {
            renderer_->begin_frame();
            renderer_->set_viewport(viewport_width_, viewport_height_);
            update_chrome_state();
            render_chrome();
            renderer_->end_textured();
            render_page();
            render_find_bar();
            renderer_->end_frame();
            // Read pixels BEFORE swapping (back buffer is still valid)
            ::glReadPixels(0, 0, sw, sh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            window_->swap_buffers();
        }

        // Save BMP

        auto wu32 = [](std::vector<u8> &d, u32 v) {
            d.push_back(v & 0xFF);
            d.push_back((v >> 8) & 0xFF);
            d.push_back((v >> 16) & 0xFF);
            d.push_back((v >> 24) & 0xFF);
        };
        auto wu16 = [](std::vector<u8> &d, u16 v) {
            d.push_back(v & 0xFF);
            d.push_back((v >> 8) & 0xFF);
        };
        std::vector<u8> bmp;
        u32 row = ((static_cast<u32>(sw) * 24 + 31) / 32) * 4;
        u32 ds = row * static_cast<u32>(sh);
        bmp.push_back('B');
        bmp.push_back('M');
        wu32(bmp, 14 + 40 + ds);
        wu32(bmp, 0);
        wu32(bmp, 14 + 40);
        wu32(bmp, 40);
        wu32(bmp, static_cast<u32>(sw));
        wu32(bmp, static_cast<u32>(sh));
        wu16(bmp, 1);
        wu16(bmp, 24);
        wu32(bmp, 0);
        wu32(bmp, ds);
        wu32(bmp, 2835);
        wu32(bmp, 2835);
        wu32(bmp, 0);
        wu32(bmp, 0);
        for (int y = 0; y < sh; y++) {
            for (int x = 0; x < sw; x++) {
                size_t idx = (static_cast<size_t>(y) * sw + static_cast<size_t>(x)) * 4;
                bmp.push_back(pixels[idx + 2]);
                bmp.push_back(pixels[idx + 1]);
                bmp.push_back(pixels[idx + 0]);
            }
            for (u32 p = static_cast<u32>(sw) * 3; p < row; p++) bmp.push_back(0);
        }
        FILE *sf = fopen(path.c_str(), "wb");
        if (sf) {
            fwrite(bmp.data(), 1, bmp.size(), sf);
            fclose(sf);
            fprintf(stderr, "Screenshot saved: %s (%dx%d)\n", path.c_str(), sw, sh);
        }
        PostQuitMessage(0);
    }

    void BrowserWindow::run() {
        window_->show();

        LARGE_INTEGER qpc_freq;
        QueryPerformanceFrequency(&qpc_freq);
        f32 qpc_to_ms = 1000.0f / (f32)qpc_freq.QuadPart;

        LARGE_INTEGER frame_start, frame_end;
        LARGE_INTEGER events_start, events_end;
        LARGE_INTEGER gpu_start, gpu_end;

        auto elapsed_qpc_ms = [&](LARGE_INTEGER start, LARGE_INTEGER end) -> f32 {
            return (f32)(end.QuadPart - start.QuadPart) * qpc_to_ms;
        };

        QueryPerformanceCounter(&frame_start);

        // Initial render
        {
            renderer_->begin_frame();
            renderer_->set_viewport(viewport_width_, viewport_height_);
            update_chrome_state();
            render_chrome();
            renderer_->end_textured();
            render_page();
            render_find_bar();
            if (chrome_.show_bookmarks_dropdown) {
                render_bookmarks_dropdown();
            }
            if (chrome_.show_settings) {
                render_settings();
            }
            if (chrome_.show_downloads) {
                render_download_panel();
            }
            if (chrome_.devtools.visible) {
                render_devtools();
            }
            renderer_->end_frame();
            window_->swap_buffers();
        }

        QueryPerformanceCounter(&frame_end);
        PerfCounters::instance().record_frame(elapsed_qpc_ms(frame_start, frame_end), 0, 0, 0, 0, 0);

        // Event loop: wait on compositor frame and window messages
        HANDLE wait_handles[] = {compositor_->frame_ready_event()};

        while (!window_->should_close()) {
            QueryPerformanceCounter(&frame_start);

            // 1. Wait for any signal (messages, compositor frame, timers)
            DWORD wait = MsgWaitForMultipleObjectsEx(
                ARRAYSIZE(wait_handles), wait_handles, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            (void)wait;

            // 2. Process pending Windows messages (non-blocking drain)
            QueryPerformanceCounter(&events_start);
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            QueryPerformanceCounter(&events_end);
            f32 events_ms = elapsed_qpc_ms(events_start, events_end);

            if (window_->should_close())
                break;

            // 3-5. Stubbed: timers, microtasks, rAF

            // Cursor blinking for form elements
            {
                static auto last_blink = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_blink).count();
                if (elapsed >= 500) {
                    html::g_form_state.caret_visible = !html::g_form_state.caret_visible;
                    last_blink = now;
                }
            }

            // Periodic session save (every 30s)
            {
                static auto last_session_save = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_session_save).count();
                if (elapsed >= 30 && session_) {
                    std::vector<SessionEntry> entries;
                    for (auto &tab : chrome_.tabs) {
                        SessionEntry e;
                        e.url = tab.url;
                        e.scroll_y = tab.scroll_y;
                        entries.push_back(e);
                    }
                    session_->save(entries);
                    last_session_save = now;
                }
            }

            // Check for deferred resize re-layout after messages are processed
            check_resize();

            // 6. Render frame
            renderer_->begin_frame();
            renderer_->set_viewport(viewport_width_, viewport_height_);
            update_chrome_state();
            render_chrome();
            renderer_->end_textured();

            render_find_bar();

            // render_page() handles both compositor and old paths internally
            render_page();
            if (chrome_.show_menu) {
                render_menu();
            }
            if (chrome_.show_bookmarks_dropdown) {
                render_bookmarks_dropdown();
            }
            if (chrome_.show_settings) {
                render_settings();
            }

            // Draw FPS overlay text inside the current frame (before end_frame flushes)
            {
                auto &pc = PerfCounters::instance();
                if (renderer_->fps_overlay_visible()) {
                    char fps_text[256];
                    snprintf(fps_text,
                             sizeof(fps_text),
                             "FPS: %.1f | min: %.1f | max: %.1f | avg: %.1f",
                             (double)pc.current_fps.load(),
                             (double)pc.min_fps.load(),
                             (double)pc.max_fps.load(),
                             (double)pc.avg_fps.load());
                    char breakdown_text[256];
                    snprintf(breakdown_text,
                             sizeof(breakdown_text),
                             "E:%.2f L:%.2f P:%.2f C:%.2f G:%.2f ms",
                             (double)pc.events_time_ms.load(),
                             (double)pc.layout_time_ms.load(),
                             (double)pc.paint_time_ms.load(),
                             (double)pc.composite_time_ms.load(),
                             (double)pc.gpu_time_ms.load());
                    f32 ox = (f32)viewport_width_ - 228.0f;
                    f32 oy = 10.0f;
                    text_renderer_->render_text(
                        renderer_.get(), fps_text, ox + 6, oy + 2, {1.0f, 1.0f, 1.0f, 1.0f}, 13);
                    text_renderer_->render_text(
                        renderer_.get(), breakdown_text, ox + 6, oy + 20, {0.8f, 0.9f, 1.0f, 1.0f}, 11);
                }
            }

            // end_frame includes FPS overlay background draw and flush
            renderer_->end_frame();

            // Record performance counters and push to renderer for next frame
            auto &pc = PerfCounters::instance();
            QueryPerformanceCounter(&gpu_start);
            window_->swap_buffers();
            QueryPerformanceCounter(&gpu_end);

            QueryPerformanceCounter(&frame_end);

            // Measure segments
            f32 dt_ms = elapsed_qpc_ms(frame_start, frame_end);
            f32 gpu_ms = elapsed_qpc_ms(gpu_start, gpu_end);

            pc.record_frame(dt_ms, events_ms, 0, 0, 0, gpu_ms);

            renderer_->set_fps_data(pc.current_fps.load(),
                                    pc.min_fps.load(),
                                    pc.max_fps.load(),
                                    pc.avg_fps.load(),
                                    pc.events_time_ms.load(),
                                    pc.layout_time_ms.load(),
                                    pc.paint_time_ms.load(),
                                    pc.composite_time_ms.load(),
                                    pc.gpu_time_ms.load());
        }
    }

    // ---------------------------------------------------------------------------
    // Resize handling — deferred re-layout after live-resize settles
    // ---------------------------------------------------------------------------
    void BrowserWindow::do_relayout() {
        if (!current_page_.has_value() || !current_page_->dom)
            return;

        // Clear selection — layout node pointers become invalid after re-layout
        selection_.clear();

        auto &page = current_page_.value();

        css::LayoutEngine layout_engine;
        layout_engine.set_text_measure(text_renderer_.get(), text_measure_cb);
        layout_engine.set_text_metrics(text_renderer_.get(), text_metrics_cb);

        auto layout_r =
            layout_engine
                .layout_async(
                    page.dom.get(), page.styles, static_cast<f32>(viewport_width_), static_cast<f32>(viewport_height_))
                .sync_wait();
        if (layout_r.is_err() || !layout_r.unwrap())
            return;

        page.layout = std::move(layout_r.unwrap());

        // Re-paint with new layout
        render::Painter painter(text_renderer_.get());
        painter.set_image_data(page.images);
        auto paint_r = painter.paint_async(page.layout.get()).sync_wait();
        if (paint_r.is_ok()) {
            page.display_list = std::move(paint_r.unwrap());
        }

        // Re-build layer tree for compositor
        page.layer_tree = render::LayerTreeBuilder::build(page.layout.get());
        if (page.layer_tree) {
            for (auto *layer : page.layer_tree->all_layers) {
                if (layer->layout_node) {
                    layer->display_list = painter.build_display_list(layer->layout_node, 0, 0);
                }
            }
        }

        // Signal the main loop to redraw
        renderer_->set_needs_redraw();
        InvalidateRect(static_cast<HWND>(window_->get_native_handle()), nullptr, FALSE);
    }

    void BrowserWindow::check_resize() {
        if (!resize_pending_)
            return;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - resize_last_time_).count();
        if (elapsed >= 100) {
            resize_pending_ = false;
            do_relayout();
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
        f32 bx = right2;
        chrome_.rects.menu = {bx - ChromeUI::BTN_SIZE, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        bx -= ChromeUI::BTN_SIZE + 2;
        chrome_.rects.download = {bx - ChromeUI::BTN_SIZE, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        bx -= ChromeUI::BTN_SIZE + 2;
        chrome_.rects.bookmark_chevron = {bx - 12, btn_y, 12, ChromeUI::BTN_SIZE};
        bx -= 14;
        chrome_.rects.bookmark = {bx - ChromeUI::BTN_SIZE, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE};
        bx -= ChromeUI::BTN_SIZE + 4;
        chrome_.rects.address = {nav_end, btn_y, bx - nav_end, ChromeUI::BTN_SIZE};
    }

    void BrowserWindow::render_chrome() {
        auto &t = theme_;
        bool active = window_->is_active();

        auto title_bg = active ? t.bg : render::Color{t.bg.r * 0.95f, t.bg.g * 0.95f, t.bg.b * 0.95f, 1.0f};
        renderer_->fill_rect(0, 0, (f32)viewport_width_, ChromeUI::TITLEBAR_H, title_bg);

        render_tabs();
        render_caption_buttons();

        renderer_->fill_rect(0, ChromeUI::TITLEBAR_H, (f32)viewport_width_, ChromeUI::TOOLBAR_H, t.bg);
        renderer_->draw_line(0, chrome_height(), (f32)viewport_width_, chrome_height(), t.border, 1.0f);
        for (f32 i = 1; i < 3; i++) {
            renderer_->draw_line(0,
                                 chrome_height() + i,
                                 (f32)viewport_width_,
                                 chrome_height() + i,
                                 {0.0f, 0.0f, 0.0f, t.shadow_alpha * (0.4f - i * 0.12f)});
        }

        render_nav_buttons();
        render_address_bar();
        render_download_button();
        render_bookmark();
        render_menu_button();
    }

    void BrowserWindow::set_theme(ThemeMode mode) {
        Theme::current = mode;
        theme_ = (mode == ThemeMode::LIGHT) ? Theme::light() : Theme::dark();
    }

    void BrowserWindow::update_chrome_state() {
        if (!chrome_.tabs.empty() && chrome_.tabs[chrome_.active_tab].history) {
            auto &tab = chrome_.tabs[chrome_.active_tab];
            chrome_.can_go_back = tab.history->can_go_back();
            chrome_.can_go_forward = tab.history->can_go_forward();
        }
        if (bookmarks_) {
            chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
        }
        if (page_loader_) {
            chrome_.is_loading = page_loader_->is_loading();
            chrome_.has_mixed_content = page_loader_->has_mixed_content();
            auto parsed = net::URL::parse(chrome_.url);
            if (parsed.is_ok()) {
                chrome_.is_https = (parsed.unwrap().scheme == "https");
            }
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
        // Only reset scroll for new navigations, not tab switches or back/forward
        // (caller is responsible for setting scroll_y when needed)
        auto &tab = chrome_.tabs[chrome_.active_tab];
        if (url != tab.url || url == "about:blank") {
            chrome_.scroll_y = 0;
        }
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
        int h = wr.bottom - wr.top;

        // All edges and corners must return the proper HT* code so the
        // window can be resized from any side.  With WS_POPUP the standard
        // non-client area is removed, so DefWindowProc cannot detect edges.

        if (my < border) {
            if (mx < border)
                return HTTOPLEFT;
            if (mx > w - border)
                return HTTOPRIGHT;
            return HTTOP;
        }
        if (my >= h - border) {
            if (mx < border)
                return HTBOTTOMLEFT;
            if (mx > w - border)
                return HTBOTTOMRIGHT;
            return HTBOTTOM;
        }
        if (mx < border)
            return HTLEFT;
        if (mx > w - border)
            return HTRIGHT;

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
