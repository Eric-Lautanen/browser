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

    std::string render_page() const;

    Result<void> save_to_file(const std::string& path);
    Result<void> load_from_file(const std::string& path);

private:
    ThemeMode theme_ = ThemeMode::LIGHT;
    std::string homepage_ = "about:blank";
    std::string search_engine_ = "google";
};

}
