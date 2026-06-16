#include "test_framework.hpp"
#include "../browser/theme.hpp"
#include "../browser/browser_window.hpp"

namespace browser {

TEST(theme_light, {
    auto t = Theme::light();
    ASSERT(t.bg.a > 0.99f);
    ASSERT(t.bg.r > 0.9f);
    ASSERT(t.text.r < 0.1f);
})

TEST(theme_dark, {
    auto t = Theme::dark();
    ASSERT(t.bg.a > 0.99f);
    ASSERT(t.bg.r < 0.2f);
    ASSERT(t.text.r > 0.9f);
})

TEST(theme_toggle, {
    auto prev = Theme::current;
    Theme::toggle();
    ASSERT(Theme::current != prev);
    Theme::toggle();
    ASSERT(Theme::current == prev);
})

TEST(theme_light_colors, {
    auto t = Theme::light();
    ASSERT(t.bookmark_fill.r > 0.9f);
    ASSERT(t.bookmark_fill.g > 0.8f);
    ASSERT(t.shadow_alpha > 0.0f);
})

TEST(theme_dark_colors, {
    auto t = Theme::dark();
    ASSERT(t.bg.r < 0.2f);
    ASSERT(t.bg.g < 0.2f);
    ASSERT(t.bg.b < 0.2f);
    ASSERT(t.shadow_alpha > 0.1f);
})

TEST(chrome_constants, {
    ASSERT(ChromeUI::TOOLBAR_H == 36.0f);
    ASSERT(ChromeUI::BTN_SIZE == 28.0f);
    ASSERT(ChromeUI::TAB_W == 32.0f);
    ASSERT(ChromeUI::NEW_TAB_W == 24.0f);
    ASSERT(ChromeUI::PADDING == 6.0f);
    ASSERT(ChromeUI::TAB_FAVICON_SIZE == 16.0f);
})

TEST(chrome_hit_test, {
    ChromeUI::ButtonRect r{10, 6, 28, 28};
    ASSERT(BrowserWindow::is_in_rect(15, 10, r));
    ASSERT(BrowserWindow::is_in_rect(10, 6, r));
    ASSERT(BrowserWindow::is_in_rect(37, 33, r));
    ASSERT(!BrowserWindow::is_in_rect(5, 5, r));
    ASSERT(!BrowserWindow::is_in_rect(9, 6, r));
    ASSERT(!BrowserWindow::is_in_rect(10, 5, r));
    ASSERT(!BrowserWindow::is_in_rect(39, 6, r));
    ASSERT(!BrowserWindow::is_in_rect(10, 35, r));
})

TEST(tab_placeholder_color, {
    std::string host = "google.com";
    u32 hash = 0;
    for (char c : host) hash = hash * 31 + static_cast<u8>(c);
    render::Color c{
        0.5f + static_cast<f32>((hash >> 16) & 0xFF) / 510.0f,
        0.5f + static_cast<f32>((hash >> 8) & 0xFF) / 510.0f,
        0.5f + static_cast<f32>(hash & 0xFF) / 510.0f,
        1.0f
    };
    ASSERT(c.r > 0.0f && c.r <= 1.0f);
    ASSERT(c.g > 0.0f && c.g <= 1.0f);
    ASSERT(c.b > 0.0f && c.b <= 1.0f);
    ASSERT(c.a == 1.0f);
    ASSERT(c.r != c.g || c.g != c.b);
})

TEST(chrome_nav_icons, {
    ASSERT(std::string("\u2190").length() > 0);
    ASSERT(std::string("\u2192").length() > 0);
    ASSERT(std::string("\u21BB").length() > 0);
    ASSERT(std::string("\u25A0").length() > 0);
})

TEST(chrome_layout_rects, {
    ChromeUI chrome;
    chrome.tabs.resize(3);
    ASSERT(chrome.tabs.size() == 3);
    ASSERT(chrome.rects.tab_close.size() == 0);
    ASSERT(!chrome.address_focused);
    ASSERT(chrome.active_tab == 0);
})

TEST(chrome_tab_operations, {
    ChromeUI chrome;
    chrome.tabs.resize(2);
    chrome.active_tab = 0;
    ASSERT(chrome.tabs.size() == 2);

    chrome.tabs.erase(chrome.tabs.begin() + static_cast<i64>(0));
    ASSERT(chrome.tabs.size() == 1);
})

TEST(chrome_button_ids, {
    ASSERT(ChromeUI::BACK == 0);
    ASSERT(ChromeUI::FORWARD == 1);
    ASSERT(ChromeUI::REFRESH == 2);
    ASSERT(ChromeUI::BOOKMARK == 3);
    ASSERT(ChromeUI::MENU == 4);
})

} // namespace browser
