#include "settings.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace browser {

SettingsManager::SettingsManager() = default;

ThemeMode SettingsManager::theme() const { return theme_; }
void SettingsManager::set_theme(ThemeMode mode) { theme_ = mode; }

std::string SettingsManager::homepage() const { return homepage_; }
void SettingsManager::set_homepage(const std::string& url) { homepage_ = url; }

std::string SettingsManager::search_engine() const { return search_engine_; }
void SettingsManager::set_search_engine(const std::string& engine) { search_engine_ = engine; }

std::string SettingsManager::render_page() const {
    std::string html = "<!DOCTYPE html><html><head><style>"
        "body{font-family:sans-serif;margin:20px;background:#f5f5f5}"
        "h1{color:#333}label{display:block;margin:12px 0 4px}"
        "a{display:inline-block;padding:6px 14px;margin:4px;text-decoration:none;"
        "background:#e0e0e0;color:#333;border-radius:4px;font-size:14px}"
        "a.sel{background:#4060e6;color:#fff}"
        ".current{color:#666;font-size:12px}"
        "</style></head><body>"
        "<h1>Settings</h1>"
        "<label>Theme</label><br>"
        "<a href=\"about:settings?theme=light\" THEME_LIGHT_CLS>Light</a>"
        "<a href=\"about:settings?theme=dark\" THEME_DARK_CLS>Dark</a><br><br>"
        "<label>Homepage</label><br>"
        "<a href=\"about:settings?homepage=about:blank\" HOMEPAGE_DEF>Default</a>"
        "<a href=\"about:settings?homepage=https://google.com\" HOMEPAGE_GOOG>Google</a><br><br>"
        "<label>Search Engine</label><br>"
        "<a href=\"about:settings?search=google\" SEARCH_GOOGLE_CLS>Google</a>"
        "<a href=\"about:settings?search=bing\" SEARCH_BING_CLS>Bing</a>"
        "<a href=\"about:settings?search=duckduckgo\" SEARCH_DDG_CLS>DuckDuckGo</a>"
        "<p class=\"current\">Changes applied immediately</p>"
        "</body></html>";

    auto replace = [&](const std::string& marker, const std::string& val) {
        for (auto pos = html.find(marker); pos != std::string::npos; pos = html.find(marker, pos)) {
            html.replace(pos, marker.length(), val);
        }
    };

    replace("THEME_LIGHT_CLS", theme_ == ThemeMode::LIGHT ? "class=\"sel\"" : "");
    replace("THEME_DARK_CLS", theme_ == ThemeMode::DARK ? "class=\"sel\"" : "");
    replace("HOMEPAGE_DEF", homepage_ == "about:blank" ? "class=\"sel\"" : "");
    replace("HOMEPAGE_GOOG", homepage_ == "https://google.com" ? "class=\"sel\"" : "");
    replace("SEARCH_GOOGLE_CLS", search_engine_ == "google" ? "class=\"sel\"" : "");
    replace("SEARCH_BING_CLS", search_engine_ == "bing" ? "class=\"sel\"" : "");
    replace("SEARCH_DDG_CLS", search_engine_ == "duckduckgo" ? "class=\"sel\"" : "");

    return html;
}

static std::string escape_pipe(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '|') r += "\\p";
        else r += c;
    }
    return r;
}

static std::string unescape_pipe(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'p') {
            r += '|';
            i++;
        } else {
            r += s[i];
        }
    }
    return r;
}

Result<void> SettingsManager::save_to_file(const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return std::string("cannot open " + path + " for writing");
    f << (theme_ == ThemeMode::LIGHT ? "light" : "dark") << "|"
      << escape_pipe(homepage_) << "|"
      << escape_pipe(search_engine_) << "\n";
    if (!f.good()) return std::string("write error");
    return {};
}

Result<void> SettingsManager::load_from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return std::string("cannot open " + path);
    std::string line;
    if (!std::getline(f, line)) return std::string("empty file");

    auto p1 = line.find('|');
    if (p1 == std::string::npos) return std::string("bad format: no first delimiter");
    auto p2 = line.find('|', p1 + 1);
    if (p2 == std::string::npos) return std::string("bad format: no second delimiter");

    std::string theme_str = line.substr(0, p1);
    std::string home_str = unescape_pipe(line.substr(p1 + 1, p2 - p1 - 1));
    std::string search_str = unescape_pipe(line.substr(p2 + 1));

    theme_ = (theme_str == "dark") ? ThemeMode::DARK : ThemeMode::LIGHT;
    homepage_ = home_str;
    search_engine_ = search_str;
    return {};
}

}
