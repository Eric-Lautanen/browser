#include "page_loader.hpp"
#include "telemetry.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include <fstream>
#include "../net/http_client.hpp"
#include "../net/tracker_blocker.hpp"
#include "../net/url.hpp"
#include "../net/deflate.hpp"
#include "../html/parser.hpp"
#include "../html/traversal.hpp"
#include "../css/parser.hpp"
#include "../css/cascade.hpp"
#include "../css/layout.hpp"
#include "../render/painter.hpp"

namespace browser {

static f32 text_measure_cb(void* ctx, const std::string& text, u32 pixel_size) {
    return static_cast<render::TextRenderer*>(ctx)->measure_text(text, pixel_size);
}

PageLoader::PageLoader(Telemetry* telemetry, SettingsManager* settings,
                       net::TrackerBlocker* tracker, render::FontManager* fm,
                       render::TextRenderer* text_renderer)
    : telemetry_(telemetry), settings_(settings), tracker_(tracker),
      fm_(fm), text_renderer_(text_renderer) {}

Result<LoadedPage> PageLoader::load(const std::string& url_str) {
    auto start = std::chrono::steady_clock::now();
    loading_ = true;

    // 1. Handle about: URLs
    if (url_str.rfind("about:", 0) == 0) {
        std::string html;
        if (url_str == "about:blank") {
            html = "<html><body></body></html>";
        } else if (url_str == "about:performance") {
            html = telemetry_->generate_report();
        } else if (url_str.rfind("about:settings", 0) == 0) {
            handle_settings_query(url_str);
            html = settings_->render_page();
        } else if (url_str.rfind("about:error", 0) == 0) {
            html = error_page(url_str);
        } else {
            html = error_page(url_str, "Unknown about: page");
        }
        auto r = load_html(html);
        loading_ = false;
        return r;
    }

    // 2. Handle file: URLs
    if (url_str.rfind("file:///", 0) == 0) {
        std::string path = url_str.substr(8); // strip "file:///"
        std::ifstream f(path);
        if (!f.is_open()) {
            loading_ = false;
            auto r = load_html(error_page(url_str, "Cannot open file: " + path));
            return r;
        }
        std::string html((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        loading_ = false;
        return load_html(html);
    }

    // 3. Parse URL (add default http:// if no scheme)
    std::string normal_url = url_str;
    if (url_str.find("://") == std::string::npos && url_str.rfind("about:", 0) != 0 &&
        url_str.rfind("file:", 0) != 0) {
        normal_url = "http://" + url_str;
    }
    auto parsed = net::URL::parse(normal_url);
    if (parsed.is_err()) {
        loading_ = false;
        auto r = load_html(error_page(url_str, "Invalid URL: " + parsed.unwrap_err()));
        return r;
    }

    // 3. Check tracker blocker before fetching
    if (tracker_ && tracker_->should_block(url_str)) {
        telemetry_->record({TelemetryEvent::TRACKER_BLOCKED, url_str, 0});
        telemetry_->set_trackers_blocked(tracker_->blocked_count());
        loading_ = false;
        auto r = load_html(error_page(url_str, "Blocked by tracker blocker"));
        return r;
    }

    net::http::Request req;
    req.method = net::http::Method::GET;
    req.url = parsed.unwrap();
    {
        std::string host_hdr = req.url.host;
        if (req.url.port != 0 && req.url.port != req.url.default_port())
            host_hdr += ":" + std::to_string(req.url.port);
        req.headers.set("Host", host_hdr);
    }
    req.headers.set("User-Agent", "Browser/0.1");
    req.headers.set("Accept", "text/html,application/xhtml+xml");
    req.headers.set("Accept-Encoding", "gzip, deflate");

    auto resp_r = http_.fetch(req);
    if (resp_r.is_err()) {
        loading_ = false;
        auto r = load_html(error_page(url_str, resp_r.unwrap_err()));
        return r;
    }
    auto resp = std::move(resp_r.unwrap());

    // 4. Handle redirects (follow 301/302/303/307/308 up to 5 times)
    u32 redirect_count = 0;
    while ((resp.status.code == 301 || resp.status.code == 302 ||
            resp.status.code == 303 || resp.status.code == 307 ||
            resp.status.code == 308) && redirect_count < 5) {
        std::string loc = resp.headers.get("Location");
        if (loc.empty()) break;
        redirect_count++;
        auto new_url = req.url.resolve(loc);
        if (new_url.is_err()) break;
        req.url = new_url.unwrap();
        {
            std::string host_hdr = req.url.host;
            if (req.url.port != 0 && req.url.port != req.url.default_port())
                host_hdr += ":" + std::to_string(req.url.port);
            req.headers.set("Host", host_hdr);
        }
        auto nr = http_.fetch(req);
        if (nr.is_err()) break;
        resp = std::move(nr.unwrap());
    }

    // 5. Decompress if Content-Encoding is gzip/deflate
    if (resp.headers.has("content-encoding")) {
        std::string ce = resp.headers.get("content-encoding");
        if (ce.find("gzip") != std::string::npos) {
            resp.body = net::gzip_decompress(resp.body.data(), static_cast<u32>(resp.body.size()));
        } else if (ce.find("deflate") != std::string::npos) {
            resp.body = net::inflate(resp.body.data(), static_cast<u32>(resp.body.size()));
        }
    }

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()),
                         resp.body.size());

    // 6. Parse HTML
    auto doc = html::parse(body_str);
    LoadedPage page;
    page.dom = std::move(doc);

    // Extract page title from <title> element
    auto* title_el = html::find_element_by_tag(page.dom.get(), "title");
    if (title_el) page.page_title = html::inner_text(title_el);

    // 7. Collect CSS
    std::string merged_css;
    collect_css(page.dom.get(), merged_css, req.url);

    // 7. Cascade
    if (!merged_css.empty()) {
        auto sheet = css::parse(merged_css);
        css::Cascade cascader;
        page.styles = cascader.compute(*page.dom, sheet);
    }

    // 8. Layout
    if (page.dom) {
        css::LayoutEngine layout_engine;
        layout_engine.set_text_measure(text_renderer_, text_measure_cb);
        page.layout = layout_engine.layout(page.dom.get(), page.styles,
                                           static_cast<f32>(viewport_width_),
                                           static_cast<f32>(viewport_height_));
    }

    // 9. Paint
    if (page.layout) {
        render::Painter painter(text_renderer_);
        painter.paint(page.layout.get());
        page.display_list = painter.display_list();
    }

    page.load_time_ms = static_cast<u32>(elapsed_ms(start));
    telemetry_->record({TelemetryEvent::PAGE_LOAD, url_str, static_cast<f64>(page.load_time_ms)});
    loading_ = false;
    return page;
}

Result<LoadedPage> PageLoader::load_html(const std::string& html) {
    auto doc = html::parse(html);
    LoadedPage page;
    page.dom = std::move(doc);

    // Extract page title from <title> element
    auto* title_el = html::find_element_by_tag(page.dom.get(), "title");
    if (title_el) page.page_title = html::inner_text(title_el);

    // Cascade UA stylesheet + any inline <style> elements
    std::string merged_css;
    net::URL empty_base;
    collect_css(page.dom.get(), merged_css, empty_base);
    if (!merged_css.empty()) {
        auto sheet = css::parse(merged_css);
        css::Cascade cascader;
        page.styles = cascader.compute(*page.dom, sheet);
    }

    css::LayoutEngine layout_engine;
    layout_engine.set_text_measure(text_renderer_, text_measure_cb);
    page.layout = layout_engine.layout(page.dom.get(), page.styles,
                                       static_cast<f32>(viewport_width_),
                                       static_cast<f32>(viewport_height_));

    if (page.layout) {
        render::Painter painter(text_renderer_);
        painter.paint(page.layout.get());
        page.display_list = painter.display_list();
    }

    return page;
}

void PageLoader::cancel() {
    loading_ = false;
}

bool PageLoader::is_loading() const {
    return loading_;
}

void PageLoader::handle_settings_query(const std::string& url_str) {
    auto qpos = url_str.find('?');
    if (qpos == std::string::npos) return;
    auto query = url_str.substr(qpos + 1);
    auto eq = query.find('=');
    if (eq == std::string::npos) return;
    auto key = query.substr(0, eq);
    auto val = net::url_decode(query.substr(eq + 1));
    if (key == "theme") {
        settings_->set_theme(val == "dark" ? ThemeMode::DARK : ThemeMode::LIGHT);
    } else if (key == "homepage") {
        settings_->set_homepage(val);
    } else if (key == "search") {
        settings_->set_search_engine(val);
    }
    settings_->save_to_file("./settings.txt");
}

static std::string html_escape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '&': r += "&amp;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

std::string PageLoader::error_page(const std::string& url, const std::string& msg) {
    std::string error_msg = msg;
    if (error_msg.empty()) {
        auto qpos = url.find('?');
        if (qpos != std::string::npos) {
            auto query = url.substr(qpos + 1);
            auto eq = query.find('=');
            if (eq != std::string::npos && query.substr(0, eq) == "msg") {
                error_msg = net::url_decode(query.substr(eq + 1));
            }
        }
        if (error_msg.empty()) error_msg = "Unknown error";
    }
    return "<html><body style='font-family:sans-serif;padding:2em'>"
           "<h2>Error loading page</h2>"
           "<p><b>URL:</b> " + html_escape(url) + "</p>"
           "<p><b>Reason:</b> " + html_escape(error_msg) + "</p>"
           "</body></html>";
}

void PageLoader::collect_css(html::Document* doc, std::string& merged_css, const net::URL& base_url) {
    html::traverse_depth_first(doc, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);
        if (el->tag_name == "style") {
            std::string css_text = html::inner_text(el);
            merged_css += css_text + "\n";
        } else if (el->tag_name == "link") {
            auto rel = el->get_attribute("rel");
            if (rel == "stylesheet") {
                auto href = el->get_attribute("href");
                if (!href.empty()) {
                    auto url_r = base_url.resolve(href);
                    if (url_r.is_ok()) {
                        auto css_url = url_r.unwrap();
                        net::http::Request css_req;
                        css_req.method = net::http::Method::GET;
                        css_req.url = css_url;
                        {
                            std::string host_hdr = css_url.host;
                            if (css_url.port != 0 && css_url.port != css_url.default_port())
                                host_hdr += ":" + std::to_string(css_url.port);
                            css_req.headers.set("Host", host_hdr);
                        }
                        auto css_resp_r = http_.fetch(css_req);
                        if (css_resp_r.is_ok()) {
                            auto& css_resp = css_resp_r.unwrap();
                            std::string css_text(reinterpret_cast<const char*>(css_resp.body.data()),
                                                  css_resp.body.size());
                            merged_css += css_text + "\n";
                        }
                    }
                }
            }
        }
    });
}

u64 PageLoader::elapsed_ms(std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

} // namespace browser
