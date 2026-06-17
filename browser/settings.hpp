#pragma once
#include <string>
#include "../tests/utility.hpp"
#include "theme.hpp"

namespace browser {

class SettingsManager {
public:
    SettingsManager();

    ThemeMode theme() const;
    void set_theme(ThemeMode mode);

    std::string homepage() const;
    void set_homepage(const std::string& url);

    std::string search_engine() const;
    void set_search_engine(const std::string& engine);

    std::string cookie_policy() const;
    void set_cookie_policy(const std::string& policy);

    u32 cache_size_mb() const;
    void set_cache_size_mb(u32 mb);

    u32 font_size() const;
    void set_font_size(u32 size);

    f32 zoom_level() const;
    void set_zoom_level(f32 level);

    std::string render_page() const;

    Result<void> save_to_file(const std::string& path);
    Result<void> load_from_file(const std::string& path);

private:
    ThemeMode theme_ = ThemeMode::LIGHT;
    std::string homepage_ = "about:blank";
    std::string search_engine_ = "google";
    std::string cookie_policy_ = "allow_all";
    u32 cache_size_mb_ = 500;
    u32 font_size_ = 16;
    f32 zoom_level_ = 1.0f;
};

}
