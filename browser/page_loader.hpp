#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <atomic>
#include <chrono>
#include "../tests/utility.hpp"
#include "../html/dom.hpp"
#include "../html/preload_scanner.hpp"
#include "../html/resource_loader.hpp"
#include "../css/layout.hpp"
#include "../css/cascade.hpp"
#include "../render/paint.hpp"
#include "../render/font_loader.hpp"
#include "../image/format.hpp"
#include "../net/http_client.hpp"
#include "../net/url.hpp"
#include "../async/task.hpp"
#include "../async/channel.hpp"

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
    std::shared_ptr<render::DisplayList> display_list;
    u32 load_time_ms = 0;
    std::string page_title;
    std::unordered_map<std::string, std::shared_ptr<image::Image>> images;
};

class PageLoader {
public:
    PageLoader(Telemetry* telemetry, SettingsManager* settings,
               net::TrackerBlocker* tracker, render::FontManager* fm,
               render::TextRenderer* text_renderer);

    void start_load(const std::string& url_str);
    void cancel();
    bool is_loading() const;
    void set_viewport_size(u32 w, u32 h) { viewport_width_ = w; viewport_height_ = h; }
    std::optional<LoadedPage> try_get_loaded_page();

private:
    net::HTTPClient http_;
    html::ResourceLoader resource_loader_;
    html::PreloadScanner preload_scanner_;
    Telemetry* telemetry_;
    SettingsManager* settings_;
    net::TrackerBlocker* tracker_;
    render::FontManager* fm_;
    render::TextRenderer* text_renderer_;
    std::atomic<bool> loading_{false};
    u32 viewport_width_ = 980;
    u32 viewport_height_ = 980;
    async::channel<LoadedPage> loaded_channel_;
    async::task<void> load_task_;

    async::task<void> load(std::string url_str);
    async::task<void> load_html(std::string html);
    void handle_settings_query(const std::string& url_str);
    std::string error_page(const std::string& url, const std::string& msg = "");
    void collect_css(html::Document* doc, std::string& merged_css, const net::URL& base_url);
    async::task<bool> fetch_css_content(std::string& merged_css);
    void collect_resources(html::Document* doc, const net::URL& base_url);
    async::task<bool> load_and_decode_images(const std::string&);
    static u64 elapsed_ms(std::chrono::steady_clock::time_point start);
    std::unordered_map<std::string, std::shared_ptr<image::Image>> loaded_images_;
};

} // namespace browser
