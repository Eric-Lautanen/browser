#include "test_framework.hpp"
#include "../browser/settings.hpp"
#include <cstdio>

namespace browser {

TEST(settings_defaults, {
    SettingsManager s;
    ASSERT(s.theme() == ThemeMode::LIGHT);
    ASSERT(s.homepage() == "about:blank");
    ASSERT(s.search_engine() == "google");
})

TEST(settings_setters, {
    SettingsManager s;
    s.set_theme(ThemeMode::DARK);
    ASSERT(s.theme() == ThemeMode::DARK);
    s.set_homepage("https://example.com");
    ASSERT(s.homepage() == "https://example.com");
    s.set_search_engine("duckduckgo");
    ASSERT(s.search_engine() == "duckduckgo");
})

TEST(settings_render_page, {
    SettingsManager s;
    auto html = s.render_page();
    ASSERT(html.find("Settings") != std::string::npos);
    ASSERT(html.find("Homepage") != std::string::npos);
    ASSERT(html.find("Search Engine") != std::string::npos);
    ASSERT(html.find("about:blank") != std::string::npos);
    ASSERT(html.find("sel") != std::string::npos);
})

TEST(settings_save_load, {
    SettingsManager s;
    s.set_theme(ThemeMode::DARK);
    s.set_homepage("https://example.com");
    s.set_search_engine("bing");
    ASSERT(s.save_to_file("test_settings.txt").is_ok());

    SettingsManager s2;
    ASSERT(s2.load_from_file("test_settings.txt").is_ok());
    ASSERT(s2.theme() == ThemeMode::DARK);
    ASSERT(s2.homepage() == "https://example.com");
    ASSERT(s2.search_engine() == "bing");

    std::remove("test_settings.txt");
})

TEST(settings_pipe_escape, {
    SettingsManager s;
    s.set_homepage("https://example.com|path");
    s.set_search_engine("a|b|c");
    ASSERT(s.save_to_file("test_settings_pipe.txt").is_ok());

    SettingsManager s2;
    ASSERT(s2.load_from_file("test_settings_pipe.txt").is_ok());
    ASSERT(s2.homepage() == "https://example.com|path");
    ASSERT(s2.search_engine() == "a|b|c");
    std::remove("test_settings_pipe.txt");
})

TEST(settings_load_missing, {
    SettingsManager s;
    auto r = s.load_from_file("nonexistent_settings.txt");
    ASSERT(r.is_err());
})

}
