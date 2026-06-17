#include "settings.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace browser {

SettingsManager::SettingsManager() = default;

ThemeMode SettingsManager::theme() const { return theme_; }
void SettingsManager::set_theme(ThemeMode mode) { theme_ = mode; }

std::string SettingsManager::homepage() const { return homepage_; }
void SettingsManager::set_homepage(const std::string& url) { homepage_ = url; }

std::string SettingsManager::search_engine() const { return search_engine_; }
void SettingsManager::set_search_engine(const std::string& engine) { search_engine_ = engine; }

std::string SettingsManager::cookie_policy() const { return cookie_policy_; }
void SettingsManager::set_cookie_policy(const std::string& policy) { cookie_policy_ = policy; }

u32 SettingsManager::cache_size_mb() const { return cache_size_mb_; }
void SettingsManager::set_cache_size_mb(u32 mb) { cache_size_mb_ = mb; }

u32 SettingsManager::font_size() const { return font_size_; }
void SettingsManager::set_font_size(u32 size) { font_size_ = size; }

f32 SettingsManager::zoom_level() const { return zoom_level_; }
void SettingsManager::set_zoom_level(f32 level) { zoom_level_ = level; }

std::string SettingsManager::download_behavior() const { return download_behavior_; }
void SettingsManager::set_download_behavior(const std::string& b) { download_behavior_ = b; }

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
        "<a href=\"about:settings?search=duckduckgo\" SEARCH_DDG_CLS>DuckDuckGo</a><br><br>"
        "<label>Cookie Policy</label><br>"
        "<a href=\"about:settings?cookie_policy=allow_all\" COOKIE_ALLOW_CLS>Allow All</a>"
        "<a href=\"about:settings?cookie_policy=block_all\" COOKIE_BLOCK_CLS>Block All</a>"
        "<a href=\"about:settings?cookie_policy=block_third_party\" COOKIE_3P_CLS>Block 3rd Party</a><br><br>"
        "<label>Font Size: FONT_SIZE_VAL px</label>"
        "<a href=\"about:settings?font_size=12\">12</a>"
        "<a href=\"about:settings?font_size=14\">14</a>"
        "<a href=\"about:settings?font_size=16\" FONT_SIZE_16_CLS>16</a>"
        "<a href=\"about:settings?font_size=18\">18</a>"
        "<a href=\"about:settings?font_size=24\">24</a><br><br>"
        "<label>Zoom: ZOOM_VAL%</label>"
        "<a href=\"about:settings?zoom=0.5\">50%</a>"
        "<a href=\"about:settings?zoom=0.75\">75%</a>"
        "<a href=\"about:settings?zoom=1.0\" ZOOM_100_CLS>100%</a>"
        "<a href=\"about:settings?zoom=1.25\">125%</a>"
        "<a href=\"about:settings?zoom=1.5\">150%</a><br><br>"
        "<label>Download Behavior</label><br>"
        "<a href=\"about:settings?download=disabled\" DL_DISABLED_CLS>Disabled</a>"
        "<a href=\"about:settings?download=notify\" DL_NOTIFY_CLS>Notify</a>"
        "<a href=\"about:settings?download=enabled\" DL_ENABLED_CLS>Enabled</a><br><br>"
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
    replace("COOKIE_ALLOW_CLS", cookie_policy_ == "allow_all" ? "class=\"sel\"" : "");
    replace("COOKIE_BLOCK_CLS", cookie_policy_ == "block_all" ? "class=\"sel\"" : "");
    replace("COOKIE_3P_CLS", cookie_policy_ == "block_third_party" ? "class=\"sel\"" : "");
    replace("FONT_SIZE_VAL", std::to_string(font_size_));
    replace("FONT_SIZE_16_CLS", font_size_ == 16 ? "class=\"sel\"" : "");
    replace("ZOOM_VAL", std::to_string(static_cast<int>(zoom_level_ * 100.0f)));
    replace("ZOOM_100_CLS", zoom_level_ == 1.0f ? "class=\"sel\"" : "");
    replace("DL_DISABLED_CLS", download_behavior_ == "disabled" ? "class=\"sel\"" : "");
    replace("DL_NOTIFY_CLS", download_behavior_ == "notify" ? "class=\"sel\"" : "");
    replace("DL_ENABLED_CLS", download_behavior_ == "enabled" ? "class=\"sel\"" : "");

    return html;
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

static void write_setting_field(std::ofstream& f, const std::string& name, const std::string& value) {
    u32 name_len = static_cast<u32>(name.size());
    u32 val_len = static_cast<u32>(value.size());
    f.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
    f.write(name.data(), name_len);
    f.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
    f.write(value.data(), val_len);
}

static bool read_setting_field(std::ifstream& f, std::string& name, std::string& value) {
    u32 name_len = 0, val_len = 0;
    f.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
    if (!f) return false;
    name.resize(name_len);
    f.read(&name[0], name_len);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
    if (!f) return false;
    value.resize(val_len);
    f.read(&value[0], val_len);
    return f.good();
}

Result<void> SettingsManager::save_to_file(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string("cannot open " + path + " for writing");

    u32 version = 1;
    f.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write settings as name-value pairs
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("theme", theme_ == ThemeMode::LIGHT ? "light" : "dark");
    fields.emplace_back("homepage", homepage_);
    fields.emplace_back("search_engine", search_engine_);
    fields.emplace_back("cookie_policy", cookie_policy_);
    fields.emplace_back("cache_size_mb", std::to_string(cache_size_mb_));
    fields.emplace_back("font_size", std::to_string(font_size_));
    fields.emplace_back("zoom_level", std::to_string(zoom_level_));
    fields.emplace_back("download_behavior", download_behavior_);

    u32 field_count = static_cast<u32>(fields.size());
    f.write(reinterpret_cast<const char*>(&field_count), sizeof(field_count));

    for (auto& [name, value] : fields) {
        write_setting_field(f, name, value);
    }

    if (!f.good()) return std::string("write error");
    return {};
}

Result<void> SettingsManager::load_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string("cannot open " + path);

    u32 version = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!f) return std::string("failed to read version");

    if (version == 1) {
        u32 field_count = 0;
        f.read(reinterpret_cast<char*>(&field_count), sizeof(field_count));
        if (!f) return std::string("failed to read field count");

        for (u32 i = 0; i < field_count; i++) {
            std::string name, value;
            if (!read_setting_field(f, name, value)) break;

            if (name == "theme") {
                theme_ = (value == "dark") ? ThemeMode::DARK : ThemeMode::LIGHT;
            } else if (name == "homepage") {
                homepage_ = value;
            } else if (name == "search_engine") {
                search_engine_ = value;
            } else if (name == "cookie_policy") {
                cookie_policy_ = value;
            } else if (name == "cache_size_mb") {
                cache_size_mb_ = static_cast<u32>(std::stoul(value));
            } else if (name == "font_size") {
                font_size_ = static_cast<u32>(std::stoul(value));
            } else if (name == "zoom_level") {
                zoom_level_ = std::stof(value);
            } else if (name == "download_behavior") {
                download_behavior_ = value;
            }
        }
        return {};
    }

    // Backward compatibility: try old pipe-delimited format
    f.clear();
    f.seekg(0, std::ios::beg);
    std::string line;
    if (!std::getline(f, line)) return std::string("empty file");

    // Trim trailing \r (binary mode on Windows keeps \r\n)
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();

    auto p1 = line.find('|');
    if (p1 == std::string::npos) return std::string("bad format: no first delimiter");
    auto p2 = line.find('|', p1 + 1);
    if (p2 == std::string::npos) return std::string("bad format: no second delimiter");

    std::string theme_str = line.substr(0, p1);
    std::string home_str = unescape_pipe(line.substr(p1 + 1, p2 - p1 - 1));
    std::string search_str = unescape_pipe(line.substr(p2 + 1));

    // Trim \r from values (for Windows binary-mode reads)
    auto trim_cr = [](std::string& s) { while (!s.empty() && s.back() == '\r') s.pop_back(); };
    trim_cr(theme_str);
    trim_cr(home_str);
    trim_cr(search_str);

    theme_ = (theme_str == "dark") ? ThemeMode::DARK : ThemeMode::LIGHT;
    homepage_ = home_str;
    search_engine_ = search_str;
    return {};
}

}
