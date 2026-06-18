#include "browser_window.hpp"
#include "history.hpp"
#include "bookmarks.hpp"
#include "telemetry.hpp"
#include "settings.hpp"
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "../platform/window.hpp"
#include "../platform/window_win32.hpp"
#include "../platform/opengl.hpp"
#include "../render/icons.hpp"

namespace pgl = browser::platform;
#include "../net/tracker_blocker.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"
#include "../render/paint_executor.hpp"

namespace browser {

namespace {

char keycode_to_char(platform::KeyCode key, bool shifted) {
    int base = static_cast<int>(platform::KeyCode::A);
    if (key >= platform::KeyCode::A && key <= platform::KeyCode::Z) {
        if (shifted) return 'A' + (static_cast<int>(key) - base);
        return 'a' + (static_cast<int>(key) - base);
    }
    int num_base = static_cast<int>(platform::KeyCode::_0);
    if (key >= platform::KeyCode::_0 && key <= platform::KeyCode::_9) {
        if (shifted) {
            const char shifted_digits[] = ")!@#$%^&*(";
            return shifted_digits[static_cast<int>(key) - num_base];
        }
        return '0' + (static_cast<int>(key) - num_base);
    }
    if (key == platform::KeyCode::SPACE) return ' ';
    if (key == platform::KeyCode::PERIOD) return shifted ? '>' : '.';
    if (key == platform::KeyCode::COMMA) return shifted ? '<' : ',';
    if (key == platform::KeyCode::SLASH) return shifted ? '?' : '/';
    if (key == platform::KeyCode::SEMICOLON) return shifted ? ':' : ';';
    if (key == platform::KeyCode::QUOTE) return shifted ? '"' : '\'';
    if (key == platform::KeyCode::BACKSLASH) return shifted ? '|' : '\\';
    if (key == platform::KeyCode::MINUS) return shifted ? '_' : '-';
    if (key == platform::KeyCode::EQUALS) return shifted ? '+' : '=';
    if (key == platform::KeyCode::LBRACKET) return shifted ? '{' : '[';
    if (key == platform::KeyCode::RBRACKET) return shifted ? '}' : ']';
    if (key == platform::KeyCode::BACKTICK) return shifted ? '~' : '`';
    return '\0';
}

}

BrowserWindow::BrowserWindow() = default;
BrowserWindow::~BrowserWindow() = default;

Result<void> BrowserWindow::initialize() {
    auto win = platform::Window::create_window("Browser", 1024, 768);
    if (win.is_err()) return win.unwrap_err();
    window_ = std::move(win.unwrap());
    window_->make_context_current();
    browser::platform::load_opengl_functions();

    tracker_ = std::make_unique<net::TrackerBlocker>();
    tracker_->load_default_list();
    net::HTTPClient::set_tracker_blocker(tracker_.get());

    telemetry_ = std::make_unique<Telemetry>();
    settings_ = std::make_unique<SettingsManager>();
    auto rs = settings_->load_from_file("browser/settings.txt");

    set_theme(settings_->theme());

    renderer_ = std::make_unique<render::Renderer>();
    auto r = renderer_->initialize(1024, 768);
    if (r.is_err()) return r.unwrap_err();

    text_renderer_ = std::make_unique<render::TextRenderer>();
    fm_ = std::make_unique<render::FontManager>();
    text_renderer_ = std::make_unique<render::TextRenderer>();
    text_renderer_->initialize(fm_.get());
            font_ok = true;
            { static FILE* f = fopen("font_debug.txt", "a"); if (f) { fprintf(f, "using embedded default font\n"); fflush(f); fclose(f); } }
        } else {
            text_renderer_->initialize(fm_.get());
            { static FILE* f = fopen("font_debug.txt", "a"); if (f) { fprintf(f, "using FontManager fallback\n"); fflush(f); fclose(f); } }
        }
    }

    page_loader_ = std::make_unique<PageLoader>(telemetry_.get(), settings_.get(), tracker_.get(),
                                                 fm_.get(), text_renderer_.get());
    page_loader_->set_viewport_size(viewport_width_, viewport_height_);

    // History is per-tab, initialized in new_tab
    bookmarks_ = std::make_unique<BookmarkManager>();
    auto rb = bookmarks_->load_from_file("browser/bookmarks.txt");

    window_->set_event_callback([this](const platform::Event& e) { handle_event(e); });

    {
        auto* w32 = static_cast<platform::Win32Window*>(window_.get());
        w32->nchittest_callback_ = [this](i32 x, i32 y) -> LRESULT {
            return hit_test_titlebar(x, y);
        };
    }

    {
        HWND hwnd = static_cast<HWND>(window_->get_native_handle());
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES };
        InitCommonControlsEx(&icex);
        tooltip_hwnd_ = CreateWindowEx(0, TOOLTIPS_CLASS, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
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
    renderer_->begin_frame();
    renderer_->set_viewport(viewport_width_, viewport_height_);
    update_chrome_state();
    // Draw chrome first so it's always visible (page draws on top but clipped)
    render_chrome();
    renderer_->end_textured();
    render_page();
    if (chrome_.show_settings) { render_settings(); }
    renderer_->end_frame();
    window_->swap_buffers();

    while (!window_->should_close()) {
        window_->pump_events();
        if (window_->should_close()) break;

        renderer_->set_needs_redraw();
        renderer_->begin_frame();
        renderer_->set_viewport(viewport_width_, viewport_height_);
        update_chrome_state();
        render_chrome();
        renderer_->end_textured();
        render_page();
        if (chrome_.show_settings) { render_settings(); }
        renderer_->end_frame();
        window_->swap_buffers();
    }
}

void BrowserWindow::navigate(const std::string& url) {
    chrome_.url = url;
    if (!chrome_.tabs.empty()) {
        chrome_.tabs[chrome_.active_tab].url = url;
        update_tab_placeholder(chrome_.active_tab);
    }
    start_load(url);
    if (!chrome_.tabs.empty() && chrome_.tabs[chrome_.active_tab].history) {
        chrome_.tabs[chrome_.active_tab].history->push(url, "");
    }
}

void BrowserWindow::navigate_back() {
    auto& tab = chrome_.tabs[chrome_.active_tab];
    if (!tab.history) return;
    auto url = tab.history->go_back();
    if (url.has_value()) {
        chrome_.url = url.value();
        if (!chrome_.tabs.empty()) {
            chrome_.tabs[chrome_.active_tab].url = url.value();
            update_tab_placeholder(chrome_.active_tab);
        }
        start_load(url.value());
    }
}

void BrowserWindow::navigate_forward() {
    auto& tab = chrome_.tabs[chrome_.active_tab];
    if (!tab.history) return;
    auto url = tab.history->go_forward();
    if (url.has_value()) {
        chrome_.url = url.value();
        if (!chrome_.tabs.empty()) {
            chrome_.tabs[chrome_.active_tab].url = url.value();
            update_tab_placeholder(chrome_.active_tab);
        }
        start_load(url.value());
    }
}

void BrowserWindow::refresh() {
    if (!chrome_.tabs.empty())
        start_load(chrome_.tabs[chrome_.active_tab].url);
}

void BrowserWindow::compute_layout() {
    f32 right = static_cast<f32>(viewport_width_);

    // --- Title bar row (y = 0..TITLEBAR_H) ---
    f32 tab_y = (ChromeUI::TITLEBAR_H - ChromeUI::BTN_SIZE) / 2.0f;
    f32 cx = ChromeUI::PADDING;

    chrome_.rects.tab_close.resize(chrome_.tabs.size());
    for (u32 i = 0; i < chrome_.tabs.size(); i++) {
        f32 tx = cx + i * (ChromeUI::TAB_W + 2);
        chrome_.rects.tab_close[i] = {tx + ChromeUI::TAB_W - 14, tab_y + 2, 12, 12};
    }
    f32 tabs_end = cx + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
    chrome_.rects.new_tab = {tabs_end, tab_y, ChromeUI::NEW_TAB_W, ChromeUI::BTN_SIZE};

    // Caption buttons — right side of title bar
    const f32 CAP_W = 46.0f, CAP_H = ChromeUI::TITLEBAR_H;
    chrome_.rects.close_btn    = { right - CAP_W,      0, CAP_W, CAP_H };
    chrome_.rects.maximize_btn = { right - CAP_W * 2,  0, CAP_W, CAP_H };
    chrome_.rects.minimize_btn = { right - CAP_W * 3,  0, CAP_W, CAP_H };

    // --- Toolbar row (y = TITLEBAR_H..CHROME_H) ---
    f32 btn_y = ChromeUI::TITLEBAR_H + (ChromeUI::TOOLBAR_H - ChromeUI::BTN_SIZE) / 2.0f;
    f32 nav_x = ChromeUI::PADDING;

    chrome_.rects.back    = { nav_x,                        btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE };
    chrome_.rects.forward = { nav_x + ChromeUI::BTN_SIZE + 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE };
    chrome_.rects.refresh = { nav_x + (ChromeUI::BTN_SIZE + 2) * 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE };
    f32 nav_end = nav_x + (ChromeUI::BTN_SIZE + 2) * 3 + ChromeUI::PADDING;

    f32 right2 = right - ChromeUI::PADDING;
    chrome_.rects.menu     = { right2 - ChromeUI::BTN_SIZE, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE };
    chrome_.rects.bookmark = { right2 - ChromeUI::BTN_SIZE * 2 - 2, btn_y, ChromeUI::BTN_SIZE, ChromeUI::BTN_SIZE };
    chrome_.rects.address  = { nav_end, btn_y, chrome_.rects.bookmark.x - 4 - nav_end, ChromeUI::BTN_SIZE };
}

void BrowserWindow::render_chrome() {
    auto& t = theme_;
    bool active = window_->is_active();

    // Title bar band
    auto title_bg = active ? t.bg : render::Color{ t.bg.r * 0.95f, t.bg.g * 0.95f, t.bg.b * 0.95f, 1.0f };
    renderer_->fill_rect(0, 0, (f32)viewport_width_, ChromeUI::TITLEBAR_H, title_bg);

    render_tabs();
    render_caption_buttons();

    // Toolbar band
    renderer_->fill_rect(0, ChromeUI::TITLEBAR_H, (f32)viewport_width_, ChromeUI::TOOLBAR_H, t.bg);
    renderer_->draw_line(0, ChromeUI::CHROME_H, (f32)viewport_width_, ChromeUI::CHROME_H, t.border, 1.0f);
    for (f32 i = 1; i < 3; i++) {
        renderer_->draw_line(0, ChromeUI::CHROME_H + i, (f32)viewport_width_, ChromeUI::CHROME_H + i,
                             {0.0f, 0.0f, 0.0f, t.shadow_alpha * (0.4f - i * 0.12f)});
    }

    render_nav_buttons();
    render_address_bar();
    render_bookmark();
    render_menu_button();
    if (chrome_.show_menu) render_menu();
}

void BrowserWindow::render_tabs() {
    auto& t = theme_;
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
            renderer_->fill_rect_tex(fx, fy, ChromeUI::TAB_FAVICON_SIZE, ChromeUI::TAB_FAVICON_SIZE, t.text, chrome_.tabs[i].favicon);
        } else {
            renderer_->fill_rect(fx, fy, ChromeUI::TAB_FAVICON_SIZE, ChromeUI::TAB_FAVICON_SIZE,
                                 chrome_.tabs[i].placeholder_color);
        }

        if (chrome_.hovered_tab == static_cast<i32>(i) || chrome_.hovered_close == static_cast<i32>(i)) {
            auto& close_r = chrome_.rects.tab_close[i];
            if (chrome_.hovered_close == static_cast<i32>(i))
                renderer_->fill_rect(close_r.x, close_r.y, close_r.w, close_r.h, t.surface_hover);
            text_renderer_->render_text(renderer_.get(), "\u00D7", close_r.x + 2, close_r.y + 1,
                                        t.text_secondary, 12);
        }
    }

    f32 nx = ChromeUI::PADDING + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
    if (chrome_.hovered_button == ChromeUI::REFRESH + 1)
        renderer_->fill_rect(nx, tab_y, ChromeUI::NEW_TAB_W, ChromeUI::BTN_SIZE, t.surface_hover);
    text_renderer_->render_text(renderer_.get(), "+", nx + 5, tab_y + 3, t.text, 16);
}

void BrowserWindow::render_caption_buttons() {
    using render::Icon;
    auto& t = theme_;
    constexpr f32 ICN = 12.0f;

    auto draw_cap = [&](const ChromeUI::ButtonRect& r, i32 id, Icon icon,
                        render::Color hover_color, render::Color icon_color) {
        if (chrome_.hovered_button == id)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, hover_color);
        renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, ICN, icon_color);
    };

    draw_cap(chrome_.rects.minimize_btn, ChromeUI::MINIMIZE, Icon::MINIMIZE,
             t.surface_hover, t.text_secondary);
    draw_cap(chrome_.rects.maximize_btn, ChromeUI::MAXIMIZE, Icon::MAXIMIZE,
             t.surface_hover, t.text_secondary);
    draw_cap(chrome_.rects.close_btn,    ChromeUI::CLOSE,    Icon::CLOSE,
             {0.9f, 0.2f, 0.2f, 1.0f}, t.text_secondary);
}

void BrowserWindow::render_nav_buttons() {
    using render::Icon;
    auto& t = theme_;
    constexpr f32 ICN = 14.0f;

    auto draw_btn = [&](const ChromeUI::ButtonRect& r, i32 id, Icon icon, bool enabled) {
        if (chrome_.hovered_button == id && enabled)
            renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
        render::Color c = enabled ? t.text : t.text_secondary;
        renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, ICN, c);
    };
    draw_btn(chrome_.rects.back,    ChromeUI::BACK,    Icon::BACK,    chrome_.can_go_back);
    draw_btn(chrome_.rects.forward, ChromeUI::FORWARD, Icon::FORWARD, chrome_.can_go_forward);

    auto& rr = chrome_.rects.refresh;
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
    auto& t = theme_;
    auto& r = chrome_.rects.address;

    // Draw outline first (extends 1px outside fill to avoid overlap artifacts)
    renderer_->stroke_rect(r.x - 1, r.y - 1, r.w + 2, r.h + 2,
                           chrome_.address_focused ? t.accent : t.border,
                           chrome_.address_focused ? 2.0f : 1.0f);
    renderer_->fill_rect(r.x, r.y, r.w, r.h,
                         chrome_.address_focused ? t.surface : t.surface_hover);

    std::string text = chrome_.address_focused ? chrome_.edit_buffer : chrome_.url;
    f32 tx = r.x + 6;
    f32 ty = r.y + 4;

    // Draw selection highlight when "all selected"
    if (chrome_.address_focused && chrome_.all_selected && !text.empty()) {
        f32 text_w = text_renderer_->measure_text(text, 13);
        renderer_->fill_rect(tx, ty, text_w, 16, {0.2f, 0.4f, 0.9f, 0.25f});
    }

    text_renderer_->render_text(renderer_.get(), text, tx, ty, t.text, 13);

    if (chrome_.address_focused) {
        // Draw cursor (blinking)
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        u64 elapsed = ms - chrome_.blink_start_ms;
        if ((elapsed / 500) % 2 == 0) {
            f32 cx = tx + text_renderer_->measure_text(
                chrome_.edit_buffer.substr(0, chrome_.cursor_pos), 13);
            renderer_->draw_line(cx, ty, cx, ty + 16, t.text, 1.5f);
        }
    }

}

void BrowserWindow::render_bookmark() {
    using render::Icon;
    auto& t = theme_;
    auto& r = chrome_.rects.bookmark;
    if (chrome_.hovered_button == ChromeUI::BOOKMARK)
        renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
    Icon icon = chrome_.is_bookmarked ? Icon::BOOKMARK_ON : Icon::BOOKMARK_OFF;
    render::Color c = chrome_.is_bookmarked ? t.bookmark_fill : t.text_secondary;
    renderer_->draw_icon_centered(icon, r.x, r.y, r.w, r.h, 14.0f, c);
}

void BrowserWindow::render_menu_button() {
    auto& t = theme_;
    auto& r = chrome_.rects.menu;
    if (chrome_.hovered_button == ChromeUI::MENU)
        renderer_->fill_rect(r.x, r.y, r.w, r.h, t.surface_hover);
    renderer_->draw_icon_centered(render::Icon::MENU, r.x, r.y, r.w, r.h, 14.0f, t.text);
}

void BrowserWindow::render_menu() {
    auto& t = theme_;
    auto& mr = chrome_.rects.menu;
    f32 mx = mr.x - 80, my = ChromeUI::CHROME_H;
    f32 mw = 160, mh = 70;

    renderer_->fill_rect(mx, my, mw, mh, t.surface);
    renderer_->stroke_rect(mx, my, mw, mh, t.border, 1.0f);
    text_renderer_->render_text(renderer_.get(), "New Tab    Ctrl+T", mx + 10, my + 6, t.text, 13);
    text_renderer_->render_text(renderer_.get(), "Settings", mx + 10, my + 28, t.text, 13);
    text_renderer_->render_text(renderer_.get(), "About",    mx + 10, my + 50, t.text_secondary, 11);
}

void BrowserWindow::render_settings() {
    auto& t = theme_;
    f32 ox = 40, oy = ChromeUI::CHROME_H + 20;
    f32 ow = static_cast<f32>(viewport_width_) - 80;
    f32 oh = static_cast<f32>(viewport_height_) - ChromeUI::CHROME_H - 60;

    renderer_->fill_rect(ox, oy, ow, oh, t.surface);
    renderer_->stroke_rect(ox, oy, ow, oh, t.border, 1.0f);

    text_renderer_->render_text(renderer_.get(), "Settings", ox + 16, oy + 12, t.text, 18);
    f32 iy = oy + 48;

    text_renderer_->render_text(renderer_.get(), "Theme", ox + 16, iy, t.text, 13);
    renderer_->fill_rect(ox + 155, iy - 2, 80, 22, t.surface_hover);
    const char* label = (Theme::current == ThemeMode::LIGHT) ? "Light" : "Dark";
    text_renderer_->render_text(renderer_.get(), label, ox + 165, iy, t.accent, 13);
    renderer_->draw_line(ox + 12, iy + 22, ox + ow - 12, iy + 22, t.border, 0.5f);

    text_renderer_->render_text(renderer_.get(), "Press Esc to close", ox + 16, oy + oh - 30,
                                t.text_secondary, 11);
}

void BrowserWindow::render_page() {
    // Check for freshly loaded pages from the async pipeline
    if (page_loader_) {
        auto loaded = page_loader_->try_get_loaded_page();
        if (loaded.has_value()) {
            current_page_ = std::move(loaded.value());
        }
    }
    if (!current_page_.has_value()) return;
    auto& page = current_page_.value();
    f32 content_h = static_cast<f32>(viewport_height_) - ChromeUI::CHROME_H;

    // Compute scroll max from page content height
    i32 page_h = 0;
    if (page.layout) page_h = static_cast<i32>(page.layout->content.height);
    chrome_.scroll_max = std::max(0, page_h - static_cast<i32>(content_h));
    chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, chrome_.scroll_y));

    // Scrollbar rect (thin bar on the right)
    f32 sb_w = 10.0f;
    chrome_.rects.scrollbar = {static_cast<f32>(viewport_width_) - sb_w, ChromeUI::CHROME_H,
                                sb_w, content_h};

    // Scissor to content area so page content never bleeds into the chrome
    pgl::glEnable(GL_SCISSOR_TEST);
    pgl::glScissor(0, 0, static_cast<GLsizei>(viewport_width_),
                   static_cast<GLsizei>(content_h));
    renderer_->fill_rect(0, ChromeUI::CHROME_H, static_cast<f32>(viewport_width_), content_h,
                         theme_.page_bg);
    renderer_->flush();
    render::PaintExecutor executor(renderer_.get(), text_renderer_.get());
    executor.set_offset(0, ChromeUI::CHROME_H - static_cast<f32>(chrome_.scroll_y));
    if (page.display_list) executor.execute(*page.display_list);

    // Draw text selection highlight (after page content, on top)
    if (selection_.active) {
        renderer_->flush();
        render::Color sel_color = {0.2f, 0.4f, 0.9f, 0.4f};
        renderer_->fill_rect(0, ChromeUI::CHROME_H, static_cast<f32>(viewport_width_), content_h, sel_color);
    }

    // Re-apply outer scissor — executor may have disabled it at end of display list
    pgl::glEnable(GL_SCISSOR_TEST);
    pgl::glScissor(0, 0, static_cast<GLsizei>(viewport_width_),
                   static_cast<GLsizei>(content_h));
    renderer_->flush();
    pgl::glDisable(GL_SCISSOR_TEST);

    render_scrollbar();
}

void BrowserWindow::render_scrollbar() {
    if (chrome_.scroll_max <= 0) return;
    auto& sb = chrome_.rects.scrollbar;
    f32 content_h = sb.h;
    // Track background
    renderer_->fill_rect(sb.x, sb.y, sb.w, sb.h, {0.85f, 0.85f, 0.85f, 1.0f});
    // Thumb
    f32 thumb_h = std::max(20.0f, sb.h * (content_h / (content_h + chrome_.scroll_max)));
    f32 thumb_y = sb.y + (static_cast<f32>(chrome_.scroll_y) / std::max(1, chrome_.scroll_max)) * (sb.h - thumb_h);
    renderer_->fill_rect(sb.x, thumb_y, sb.w, thumb_h, {0.5f, 0.5f, 0.5f, 1.0f});
}

void BrowserWindow::handle_event(const platform::Event& e) {
    if (e.type == platform::Event::Type::KEY_DOWN) {
        if (e.key == platform::KeyCode::CTRL || e.key == platform::KeyCode::LCTRL || e.key == platform::KeyCode::RCTRL)
            chrome_.ctrl_down = true;
        else if (e.key == platform::KeyCode::SHIFT || e.key == platform::KeyCode::LSHIFT || e.key == platform::KeyCode::RSHIFT)
            chrome_.shift_down = true;
        else if (e.key == platform::KeyCode::ALT || e.key == platform::KeyCode::LALT || e.key == platform::KeyCode::RALT)
            chrome_.alt_down = true;
        handle_key_down(e);
    } else if (e.type == platform::Event::Type::KEY_UP) {
        if (e.key == platform::KeyCode::CTRL || e.key == platform::KeyCode::LCTRL || e.key == platform::KeyCode::RCTRL)
            chrome_.ctrl_down = false;
        else if (e.key == platform::KeyCode::SHIFT || e.key == platform::KeyCode::LSHIFT || e.key == platform::KeyCode::RSHIFT)
            chrome_.shift_down = false;
        else if (e.key == platform::KeyCode::ALT || e.key == platform::KeyCode::LALT || e.key == platform::KeyCode::RALT)
            chrome_.alt_down = false;
    } else if (e.type == platform::Event::Type::MOUSE_DOWN) {
        handle_mouse_click(e.mouse_x, e.mouse_y);
    } else if (e.type == platform::Event::Type::MOUSE_UP) {
        chrome_.scroll_dragging = false;
    } else if (e.type == platform::Event::Type::MOUSE_MOVE) {
        if (chrome_.scroll_dragging) {
            f32 sb_h = chrome_.rects.scrollbar.h;
            f32 thumb_h = std::max(20.0f, sb_h * sb_h / (sb_h + chrome_.scroll_max));
            f32 range = sb_h - thumb_h;
            if (range > 0) {
                f32 delta = static_cast<f32>(e.mouse_y - chrome_.scroll_drag_start_y);
                chrome_.scroll_y = static_cast<i32>(chrome_.scroll_drag_start_pos + delta * chrome_.scroll_max / range);
                chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max, chrome_.scroll_y));
            }
        }
        handle_mouse_move(e.mouse_x, e.mouse_y);
    } else if (e.type == platform::Event::Type::MOUSE_SCROLL) {
        handle_scroll(e.scroll_delta);
    } else if (e.type == platform::Event::Type::WINDOW_RESIZE) {
        viewport_width_ = e.width;
        viewport_height_ = e.height;
        renderer_->set_viewport(e.width, e.height);
        compute_layout();
        if (page_loader_) page_loader_->set_viewport_size(viewport_width_, viewport_height_);
    }
}

void BrowserWindow::handle_mouse_click(i32 mx, i32 my) {
    { static FILE* f = nullptr; if (!f) { f = fopen("click_debug.txt", "w"); } if (f) { fprintf(f, "click: mx=%d my=%d chrome_h=%.0f show_settings=%d show_menu=%d address_focused=%d\n", mx, my, ChromeUI::CHROME_H, (int)chrome_.show_settings, (int)chrome_.show_menu, (int)chrome_.address_focused); fflush(f); } }
    if (my > ChromeUI::CHROME_H) {
        if (chrome_.show_settings) {
            f32 ox = 40, oy = ChromeUI::CHROME_H + 20;
            if (is_in_rect(mx, my, {ox + 155, oy + 46, 80, 22})) {
                Theme::toggle();
                set_theme(Theme::current);
                if (settings_) {
                    settings_->set_theme(Theme::current);
                    settings_->save_to_file("browser/settings.txt");
                }
                chrome_.address_focused = false;
                return;
            }
        }
        // Scrollbar click
        if (chrome_.scroll_max > 0 && is_in_rect(mx, my, chrome_.rects.scrollbar)) {
            chrome_.scroll_dragging = true;
            chrome_.scroll_drag_start_y = my;
            chrome_.scroll_drag_start_pos = chrome_.scroll_y;
            chrome_.address_focused = false;
            return;
        }
        chrome_.address_focused = false;
        return;
    }

    auto& r = chrome_.rects;

    // Caption buttons
    if (is_in_rect(mx, my, r.close_btn)) {
        SendMessage((HWND)window_->get_native_handle(), WM_CLOSE, 0, 0);
        return;
    }
    if (is_in_rect(mx, my, r.maximize_btn)) {
        HWND hwnd = (HWND)window_->get_native_handle();
        IsZoomed(hwnd) ? ShowWindow(hwnd, SW_RESTORE) : ShowWindow(hwnd, SW_MAXIMIZE);
        return;
    }
    if (is_in_rect(mx, my, r.minimize_btn)) {
        ShowWindow((HWND)window_->get_native_handle(), SW_MINIMIZE);
        return;
    }

    for (u32 i = 0; i < r.tab_close.size(); i++) {
        if (is_in_rect(mx, my, r.tab_close[i])) { close_tab(i); return; }
    }

    f32 tab_start = ChromeUI::PADDING;
    for (u32 i = 0; i < chrome_.tabs.size(); i++) {
        if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
            mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
            if (chrome_.active_tab != i) {
                // Save scroll for current tab
                chrome_.tabs[chrome_.active_tab].scroll_y = chrome_.scroll_y;
                chrome_.active_tab = i;
                // Restore scroll for new tab
                chrome_.scroll_y = chrome_.tabs[i].scroll_y;
                start_load(chrome_.tabs[i].url);
            }
            return;
        }
    }

    f32 new_tab_x = tab_start + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
    if (mx >= new_tab_x && mx <= new_tab_x + ChromeUI::NEW_TAB_W) {
        new_tab(); return;
    }

    if (is_in_rect(mx, my, r.back))         { navigate_back(); return; }
    if (is_in_rect(mx, my, r.forward))      { navigate_forward(); return; }
    if (is_in_rect(mx, my, r.refresh))      { refresh(); return; }

    if (is_in_rect(mx, my, r.address)) {
        chrome_.address_focused = true;
        chrome_.edit_buffer = chrome_.url;
        chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.length());
        auto now = std::chrono::steady_clock::now();
        chrome_.blink_start_ms = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        return;
    }

    if (is_in_rect(mx, my, r.bookmark))     { handle_bookmark_click(); return; }

    if (is_in_rect(mx, my, r.menu))         { chrome_.show_menu = !chrome_.show_menu; return; }

    if (chrome_.show_menu) {
        f32 mx2 = r.menu.x - 80, my2 = ChromeUI::CHROME_H;
        f32 my_off = my - my2;
        if (mx >= mx2 && mx <= mx2 + 160 && my_off >= 0 && my_off <= 70) {
            if (my_off < 22) new_tab();
            else if (my_off < 44) chrome_.show_settings = true;
            chrome_.show_menu = false;
            return;
        }
        chrome_.show_menu = false;
    }

    chrome_.address_focused = false;
}

static void clipboard_copy(const std::string& text) {
    if (text.empty()) return;
    HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!hglb) return;
    char* buf = (char*)GlobalLock(hglb);
    if (buf) { memcpy(buf, text.data(), text.size()); buf[text.size()] = 0; }
    GlobalUnlock(hglb);
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hglb);
        CloseClipboard();
    } else {
        GlobalFree(hglb);
    }
}

static std::string clipboard_paste() {
    std::string result;
    if (OpenClipboard(nullptr)) {
        HANDLE h = GetClipboardData(CF_TEXT);
        if (h) {
            char* text = (char*)GlobalLock(h);
            if (text) { result = text; }
            GlobalUnlock(h);
        }
        CloseClipboard();
    }
    return result;
}

void BrowserWindow::handle_key_down(const platform::Event& e) {
    // Global shortcuts (work regardless of focus)
    if (e.key == platform::KeyCode::T && chrome_.ctrl_down) { new_tab(); return; }
    if (e.key == platform::KeyCode::R && chrome_.ctrl_down) { refresh(); return; }
    if (e.key == platform::KeyCode::F5 && !chrome_.ctrl_down) { refresh(); return; }
    if (e.key == platform::KeyCode::L && chrome_.ctrl_down) {
        chrome_.address_focused = true;
        chrome_.edit_buffer = chrome_.url;
        chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
        chrome_.sel_start = 0;
        chrome_.all_selected = false;
        auto now = std::chrono::steady_clock::now();
        chrome_.blink_start_ms = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        return;
    }
    if (e.key == platform::KeyCode::F6 && !chrome_.ctrl_down) {
        chrome_.address_focused = true;
        chrome_.edit_buffer = chrome_.url;
        chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
        chrome_.sel_start = 0;
        chrome_.all_selected = false;
        return;
    }
    if ((e.key == platform::KeyCode::LEFT && chrome_.alt_down) ||
        (e.key == platform::KeyCode::BACKSPACE && chrome_.alt_down)) {
        navigate_back(); return;
    }
    if (e.key == platform::KeyCode::RIGHT && chrome_.alt_down) {
        navigate_forward(); return;
    }

    if (chrome_.address_focused) {
        // Address-bar shortcuts when focused
        if (e.key == platform::KeyCode::W && chrome_.ctrl_down) { close_tab(chrome_.active_tab); return; }
        if (e.key == platform::KeyCode::C && chrome_.ctrl_down) {
            clipboard_copy(chrome_.edit_buffer);
            return;
        }
        if (e.key == platform::KeyCode::X && chrome_.ctrl_down) {
            clipboard_copy(chrome_.edit_buffer);
            chrome_.edit_buffer.clear();
            chrome_.cursor_pos = 0;
            return;
        }
        if (e.key == platform::KeyCode::V && chrome_.ctrl_down) {
            std::string paste = clipboard_paste();
            if (!paste.empty()) {
                if (chrome_.all_selected) { chrome_.edit_buffer = paste; chrome_.cursor_pos = static_cast<u32>(paste.size()); chrome_.all_selected = false; }
                else { chrome_.edit_buffer.insert(chrome_.cursor_pos, paste); chrome_.cursor_pos += static_cast<u32>(paste.size()); }
            }
            return;
        }
        if (e.key == platform::KeyCode::A && chrome_.ctrl_down) {
            chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.size());
            chrome_.sel_start = 0;
            chrome_.all_selected = true;
            return;
        }

        // Standard address bar editing
        if (e.key == platform::KeyCode::ENTER) {
            navigate(chrome_.edit_buffer);
            chrome_.address_focused = false;
            chrome_.edit_buffer.clear();
            chrome_.all_selected = false;
        } else if (e.key == platform::KeyCode::ESCAPE) {
            chrome_.address_focused = false;
            chrome_.edit_buffer.clear();
            chrome_.all_selected = false;
        } else if ((e.key == platform::KeyCode::BACKSPACE)) {
            if (chrome_.all_selected) { chrome_.edit_buffer.clear(); chrome_.cursor_pos = 0; chrome_.all_selected = false; }
            else if (chrome_.cursor_pos > 0) {
                chrome_.edit_buffer.erase(chrome_.cursor_pos - 1, 1);
                chrome_.cursor_pos--;
            }
        } else if (e.key == platform::KeyCode::DELETE && !chrome_.ctrl_down) {
            if (chrome_.all_selected) { chrome_.edit_buffer.clear(); chrome_.cursor_pos = 0; chrome_.all_selected = false; }
            else if (chrome_.cursor_pos < chrome_.edit_buffer.length()) {
                chrome_.edit_buffer.erase(chrome_.cursor_pos, 1);
            }
        } else if (e.key == platform::KeyCode::LEFT && !chrome_.ctrl_down) {
            if (chrome_.cursor_pos > 0) chrome_.cursor_pos--;
            chrome_.all_selected = false;
        } else if (e.key == platform::KeyCode::RIGHT && !chrome_.ctrl_down) {
            if (chrome_.cursor_pos < chrome_.edit_buffer.length()) chrome_.cursor_pos++;
            chrome_.all_selected = false;
        } else if (e.key == platform::KeyCode::HOME) {
            chrome_.cursor_pos = 0;
            chrome_.all_selected = false;
        } else if (e.key == platform::KeyCode::END) {
            chrome_.cursor_pos = static_cast<u32>(chrome_.edit_buffer.length());
            chrome_.all_selected = false;
        } else {
            char c = keycode_to_char(e.key, chrome_.shift_down);
            if (c) {
                if (chrome_.all_selected) { chrome_.edit_buffer.clear(); chrome_.cursor_pos = 0; chrome_.all_selected = false; }
                chrome_.edit_buffer.insert(chrome_.cursor_pos, 1, c);
                chrome_.cursor_pos++;
            }
        }
    } else {
        if (e.key == platform::KeyCode::ESCAPE) {
            chrome_.show_settings = false;
            chrome_.show_menu = false;
        }
        // Keyboard scrolling
        f32 content_h = static_cast<f32>(viewport_height_) - ChromeUI::CHROME_H;
        i32 page_step = static_cast<i32>(content_h * 0.9f);
        if (e.key == platform::KeyCode::PAGE_DOWN ||
            (e.key == platform::KeyCode::SPACE && !chrome_.address_focused)) {
            handle_scroll(-1 * page_step / 30);
        } else if (e.key == platform::KeyCode::PAGE_UP) {
            handle_scroll(1 * page_step / 30);
        } else if (e.key == platform::KeyCode::HOME) {
            chrome_.scroll_y = 0;
        } else if (e.key == platform::KeyCode::END) {
            chrome_.scroll_y = chrome_.scroll_max;
        } else if (e.key == platform::KeyCode::UP) {
            handle_scroll(3);  // scroll up 90px
        } else if (e.key == platform::KeyCode::DOWN) {
            handle_scroll(-3); // scroll down 90px
        }
    }
}

void BrowserWindow::handle_mouse_move(i32 mx, i32 my) {
    chrome_.hovered_button = -1;
    chrome_.hovered_tab = -1;
    chrome_.hovered_close = -1;
    if (my > ChromeUI::CHROME_H) {
        update_tab_tooltip(-1, -1);
        return;
    }
    update_tab_tooltip(mx, my);

    for (u32 i = 0; i < chrome_.rects.tab_close.size(); i++) {
        if (is_in_rect(mx, my, chrome_.rects.tab_close[i])) {
            chrome_.hovered_close = static_cast<i32>(i); return;
        }
    }

    f32 tab_start = ChromeUI::PADDING;
    for (u32 i = 0; i < chrome_.tabs.size(); i++) {
        if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
            mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
            chrome_.hovered_tab = static_cast<i32>(i); return;
        }
    }

    auto& r = chrome_.rects;
    if (is_in_rect(mx, my, r.close_btn))        chrome_.hovered_button = ChromeUI::CLOSE;
    else if (is_in_rect(mx, my, r.maximize_btn)) chrome_.hovered_button = ChromeUI::MAXIMIZE;
    else if (is_in_rect(mx, my, r.minimize_btn)) chrome_.hovered_button = ChromeUI::MINIMIZE;
    else if (is_in_rect(mx, my, r.back))         chrome_.hovered_button = ChromeUI::BACK;
    else if (is_in_rect(mx, my, r.forward))      chrome_.hovered_button = ChromeUI::FORWARD;
    else if (is_in_rect(mx, my, r.refresh))      chrome_.hovered_button = ChromeUI::REFRESH;
    else if (is_in_rect(mx, my, r.bookmark))     chrome_.hovered_button = ChromeUI::BOOKMARK;
    else if (is_in_rect(mx, my, r.menu))         chrome_.hovered_button = ChromeUI::MENU;

    f32 ntx = tab_start + chrome_.tabs.size() * (ChromeUI::TAB_W + 2);
    if (mx >= ntx && mx <= ntx + ChromeUI::NEW_TAB_W)
        chrome_.hovered_button = ChromeUI::REFRESH + 1;
}

void BrowserWindow::update_tab_tooltip(i32 mx, i32 my) {
    HWND hwnd = static_cast<HWND>(window_->get_native_handle());
    TOOLINFO ti = {};
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_TRACK | TTF_IDISHWND;
    ti.hwnd = hwnd;
    ti.uId = reinterpret_cast<UINT_PTR>(hwnd);

    i32 tab_idx = -1;
    if (my >= 0 && my < (i32)ChromeUI::CHROME_H) {
        // Check close button first
        for (u32 i = 0; i < chrome_.rects.tab_close.size(); i++) {
            if (is_in_rect(mx, my, chrome_.rects.tab_close[i])) { tab_idx = (i32)i; break; }
        }
        if (tab_idx < 0) {
            f32 tab_start = ChromeUI::PADDING;
            for (u32 i = 0; i < chrome_.tabs.size(); i++) {
                if (mx >= tab_start + i * (ChromeUI::TAB_W + 2) &&
                    mx <= tab_start + i * (ChromeUI::TAB_W + 2) + ChromeUI::TAB_W) {
                    tab_idx = (i32)i; break;
                }
            }
        }
    }

    if (tab_idx >= 0 && tab_idx < (i32)chrome_.tabs.size()) {
        tooltip_text_ = chrome_.tabs[tab_idx].url;
        if (tooltip_text_.length() > 60) {
            tooltip_text_ = tooltip_text_.substr(0, 57) + "...";
        }
        ti.lpszText = const_cast<char*>(tooltip_text_.c_str());
        SendMessage(tooltip_hwnd_, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessage(tooltip_hwnd_, TTM_TRACKPOSITION, 0, MAKELPARAM(mx, my + 24));
        SendMessage(tooltip_hwnd_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
    } else {
        SendMessage(tooltip_hwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
    }
}

void BrowserWindow::handle_scroll(i32 delta) {
    chrome_.scroll_y = std::max(0, std::min(chrome_.scroll_max,
        static_cast<i32>(chrome_.scroll_y - delta * 30)));
}

void BrowserWindow::start_load(const std::string& url) {
    chrome_.scroll_y = 0;
    if (page_loader_) {
        page_loader_->start_load(url);
    }
    if (telemetry_ && tracker_) {
        telemetry_->set_trackers_blocked(tracker_->blocked_count());
    }
}

bool BrowserWindow::is_in_rect(i32 x, i32 y, const ChromeUI::ButtonRect& r) {
    return static_cast<f32>(x) >= r.x && static_cast<f32>(x) <= r.x + r.w &&
           static_cast<f32>(y) >= r.y && static_cast<f32>(y) <= r.y + r.h;
}

void BrowserWindow::set_theme(ThemeMode mode) {
    Theme::current = mode;
    theme_ = (mode == ThemeMode::LIGHT) ? Theme::light() : Theme::dark();
}

void BrowserWindow::new_tab(const std::string& url) {
    TabInfo tab;
    tab.url = url;
    tab.placeholder_color = {0.7f, 0.7f, 0.7f, 1.0f};
    tab.history = std::make_unique<HistoryManager>();
    chrome_.tabs.push_back(std::move(tab));
    chrome_.active_tab = static_cast<u32>(chrome_.tabs.size()) - 1;
    compute_layout();
    update_tab_placeholder(chrome_.active_tab);
    chrome_.url = url;
    start_load(url);
    chrome_.tabs[chrome_.active_tab].history->push(url, "");
}

void BrowserWindow::close_tab(u32 index) {
    if (chrome_.tabs.size() <= 1) return;
    chrome_.tabs.erase(chrome_.tabs.begin() + static_cast<i64>(index));
    if (chrome_.active_tab >= index && chrome_.active_tab > 0)
        chrome_.active_tab--;
    compute_layout();
    start_load(chrome_.tabs[chrome_.active_tab].url);
}

void BrowserWindow::update_tab_placeholder(u32 index) {
    auto& tab = chrome_.tabs[index];
    auto url = net::URL::parse(tab.url);
    if (url.is_err()) return;
    auto host = url.unwrap().host;
    u32 hash = 0;
    for (char c : host) hash = hash * 31 + static_cast<u8>(c);
    tab.placeholder_color = {
        0.5f + static_cast<f32>((hash >> 16) & 0xFF) / 510.0f,
        0.5f + static_cast<f32>((hash >> 8) & 0xFF) / 510.0f,
        0.5f + static_cast<f32>(hash & 0xFF) / 510.0f,
        1.0f
    };
}

void BrowserWindow::handle_bookmark_click() {
    if (!bookmarks_) return;
    if (bookmarks_->is_bookmarked(chrome_.url)) {
        bookmarks_->remove(chrome_.url);
    } else {
        std::string title = chrome_.url;
        if (current_page_.has_value()) {
            auto& p = current_page_.value();
            if (!p.page_title.empty()) title = p.page_title;
        }
        bookmarks_->add(chrome_.url, title);
    }
    bookmarks_->save_to_file("browser/bookmarks.txt");
    chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
}

void BrowserWindow::update_chrome_state() {
    if (!chrome_.tabs.empty() && chrome_.tabs[chrome_.active_tab].history) {
        auto& tab = chrome_.tabs[chrome_.active_tab];
        chrome_.can_go_back = tab.history->can_go_back();
        chrome_.can_go_forward = tab.history->can_go_forward();
    }
    if (bookmarks_) {
        chrome_.is_bookmarked = bookmarks_->is_bookmarked(chrome_.url);
    }
    if (page_loader_) {
        chrome_.is_loading = page_loader_->is_loading();
    }
}

LRESULT BrowserWindow::hit_test_titlebar(i32 mx, i32 my) {
    const int border = 6;
    RECT wr;
    GetWindowRect(static_cast<HWND>(window_->get_native_handle()), &wr);
    int w = wr.right - wr.left;

    if (my < border) {
        if (mx < border)       return HTTOPLEFT;
        if (mx > w - border)   return HTTOPRIGHT;
        return HTTOP;
    }
    if (my >= (i32)ChromeUI::TITLEBAR_H) return HTCLIENT;

    auto& r = chrome_.rects;
    if (is_in_rect(mx, my, r.close_btn) ||
        is_in_rect(mx, my, r.maximize_btn) ||
        is_in_rect(mx, my, r.minimize_btn)) return HTCLIENT;

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

} // namespace browser
