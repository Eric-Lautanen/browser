#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <atomic>
#include <chrono>
#include "../tests/utility.hpp"
#include "../html/dom.hpp"
#include "../css/layout.hpp"
#include "../css/cascade.hpp"
#include "../render/paint.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"

namespace browser {

class Telemetry;
class SettingsManager;

namespace net { class TrackerBlocker; }
namespace render { class FontManager; }
namespace render { class TextRenderer; }

struct LoadedPage {
    std::unique_ptr<html::Document> dom;
    std::unordered_map<const html::Element*, css::ComputedStyle> styles;
    std::unique_ptr<css::LayoutNode> layout;
    render::DisplayList display_list;
    u32 load_time_ms = 0;
    std::string page_title;
};

class PageLoader {
public:
    PageLoader(Telemetry* telemetry, SettingsManager* settings,
               net::TrackerBlocker* tracker, render::FontManager* fm,
               render::TextRenderer* text_renderer);

    Result<LoadedPage> load(const std::string& url_str);
    Result<LoadedPage> load_html(const std::string& html);
    void cancel();
    bool is_loading() const;
    void set_viewport_size(u32 w, u32 h) { viewport_width_ = w; viewport_height_ = h; }

private:
    net::HTTPClient http_;
    Telemetry* telemetry_;
    SettingsManager* settings_;
    net::TrackerBlocker* tracker_;
    render::FontManager* fm_;
    render::TextRenderer* text_renderer_;
    std::atomic<bool> loading_{false};
    u32 viewport_width_ = 980;
    u32 viewport_height_ = 980;

    void handle_settings_query(const std::string& url_str);
    std::string error_page(const std::string& url, const std::string& msg = "");
    void collect_css(html::Document* doc, std::string& merged_css, const net::URL& base_url);
    static u64 elapsed_ms(std::chrono::steady_clock::time_point start);
};

} // namespace browser
