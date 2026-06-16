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
#include "../image/decoder.hpp"
#include "../async/executor.hpp"

namespace browser {

static f32 text_measure_cb(void* ctx, const std::string& text, u32 pixel_size) {
    return static_cast<render::TextRenderer*>(ctx)->measure_text(text, pixel_size);
}

PageLoader::PageLoader(Telemetry* telemetry, SettingsManager* settings,
                       net::TrackerBlocker* tracker, render::FontManager* fm,
                       render::TextRenderer* text_renderer)
    : resource_loader_(&http_), telemetry_(telemetry), settings_(settings), tracker_(tracker),
      fm_(fm), text_renderer_(text_renderer), loaded_channel_(1) {}

void PageLoader::start_load(const std::string& url_str) {
    if (loading_.exchange(true)) return;
    load_task_ = load(url_str);
    load_task_.start();
}

std::optional<LoadedPage> PageLoader::try_get_loaded_page() {
    return loaded_channel_.try_receive();
}

async::task<void> PageLoader::load(std::string url_str) {
    co_await async::thread_pool_executor{};
    auto start = std::chrono::steady_clock::now();

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
        co_await load_html(html);
        loading_ = false;
        co_return;
    }

    if (url_str.rfind("file:///", 0) == 0) {
        std::string path = url_str.substr(8);
        std::ifstream f(path);
        if (!f.is_open()) {
            co_await load_html(error_page(url_str, "Cannot open file: " + path));
            loading_ = false;
            co_return;
        }
        std::string html((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        co_await load_html(html);
        loading_ = false;
        co_return;
    }

    std::string normal_url = url_str;
    if (url_str.find("://") == std::string::npos && url_str.rfind("about:", 0) != 0 &&
        url_str.rfind("file:", 0) != 0) {
        normal_url = "http://" + url_str;
    }
    auto parsed = net::URL::parse(normal_url);
    if (parsed.is_err()) {
        co_await load_html(error_page(url_str, "Invalid URL: " + parsed.unwrap_err()));
        loading_ = false;
        co_return;
    }

    if (tracker_ && tracker_->should_block(url_str)) {
        telemetry_->record({TelemetryEvent::TRACKER_BLOCKED, url_str, 0});
        telemetry_->set_trackers_blocked(tracker_->blocked_count());
        co_await load_html(error_page(url_str, "Blocked by tracker blocker"));
        loading_ = false;
        co_return;
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

    auto resp_r = co_await http_.fetch_async(req);
    if (resp_r.is_err()) {
        co_await load_html(error_page(url_str, resp_r.unwrap_err()));
        loading_ = false;
        co_return;
    }
    auto resp = std::move(resp_r.unwrap());

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
        auto nr = co_await http_.fetch_async(req);
        if (nr.is_err()) break;
        resp = std::move(nr.unwrap());
    }

    if (resp.headers.has("content-encoding")) {
        co_await async::thread_pool_executor{};
        std::string ce = resp.headers.get("content-encoding");
        if (ce.find("gzip") != std::string::npos) {
            resp.body = net::gzip_decompress(resp.body.data(), static_cast<u32>(resp.body.size()));
        } else if (ce.find("deflate") != std::string::npos) {
            resp.body = net::inflate(resp.body.data(), static_cast<u32>(resp.body.size()));
        }
    }

    std::string body_str(reinterpret_cast<const char*>(resp.body.data()),
                         resp.body.size());

    std::string base_url_str = req.url.to_string();
    loaded_images_.clear();
    resource_loader_ = html::ResourceLoader(&http_);

    preload_scanner_ = html::PreloadScanner();
    preload_scanner_.set_fetch_callback([&](const html::PreloadRequest& preq) {
        html::ResourcePriority prio = html::ResourcePriority::IMAGE;
        if (preq.as == "style") prio = html::ResourcePriority::CSS;
        else if (preq.as == "script") prio = html::ResourcePriority::JS;
        else if (preq.as == "font") prio = html::ResourcePriority::FONT;
        resource_loader_.request({preq.url, prio, preq.is_async, preq.is_defer, preq.is_module});
    });

    auto doc_r = co_await html::parse_async(body_str, &preload_scanner_, base_url_str);
    if (doc_r.is_err()) {
        loading_ = false;
        co_return;
    }

    LoadedPage page;
    page.dom = std::move(doc_r.unwrap());
    auto* title_el = html::find_element_by_tag(page.dom.get(), "title");
    if (title_el) page.page_title = html::inner_text(title_el);

    std::string merged_css;
    collect_css(page.dom.get(), merged_css, req.url);
    co_await fetch_css_content(merged_css);

    css::Cascade cascader;
    css::StyleSheet sheet;

    if (!merged_css.empty()) {
        auto sheet_r = co_await css::parse_async(merged_css);
        if (sheet_r.is_ok()) {
            sheet = std::move(sheet_r.unwrap());
            auto styles_r = co_await cascader.compute_async(*page.dom, sheet);
            if (styles_r.is_ok()) {
                page.styles = std::move(styles_r.unwrap().element_styles);
            }
        }
    }

    // Load fonts from @font-face rules
    render::FontLoader font_loader(fm_, &http_);
    font_loader.load_from_stylesheet(sheet);
    font_loader.load_from_at_rules(sheet.at_rules);
    co_await font_loader.fetch_all(base_url_str);

    // Load and decode images
    collect_resources(page.dom.get(), req.url);
    co_await load_and_decode_images(base_url_str);
    page.images = loaded_images_;

    if (page.dom) {
        css::LayoutEngine layout_engine;
        layout_engine.set_text_measure(text_renderer_, text_measure_cb);
        auto layout_r = co_await layout_engine.layout_async(page.dom.get(), page.styles,
                                                     static_cast<f32>(viewport_width_),
                                                     static_cast<f32>(viewport_height_));
        if (layout_r.is_ok()) {
            page.layout = std::move(layout_r.unwrap());
        }
    }

    if (page.layout) {
        render::Painter painter(text_renderer_);
        painter.set_image_data(page.images);
        auto paint_r = co_await painter.paint_async(page.layout.get());
        if (paint_r.is_ok()) {
            page.display_list = std::move(paint_r.unwrap());
        }
    }

    page.load_time_ms = static_cast<u32>(elapsed_ms(start));
    telemetry_->record({TelemetryEvent::PAGE_LOAD, url_str, static_cast<f64>(page.load_time_ms)});

    loaded_channel_.send(std::move(page));
    loading_ = false;
    co_return;
}

async::task<void> PageLoader::load_html(std::string html) {
    co_await async::thread_pool_executor{};
    auto start = std::chrono::steady_clock::now();

    auto doc_r = co_await browser::html::parse_async(html);
    if (doc_r.is_err()) {
        loaded_channel_.send(LoadedPage{});
        co_return;
    }

    LoadedPage page;
    page.dom = std::move(doc_r.unwrap());
    auto* title_el = html::find_element_by_tag(page.dom.get(), "title");
    if (title_el) page.page_title = html::inner_text(title_el);

    std::string merged_css;
    net::URL empty_base;
    collect_css(page.dom.get(), merged_css, empty_base);

    css::Cascade cascader;
    if (!merged_css.empty()) {
        auto sheet_r = co_await css::parse_async(merged_css);
        if (sheet_r.is_ok()) {
            auto styles_r = co_await cascader.compute_async(*page.dom, sheet_r.unwrap());
            if (styles_r.is_ok()) {
                page.styles = std::move(styles_r.unwrap().element_styles);
            }
        }
    }

    if (page.dom) {
        css::LayoutEngine layout_engine;
        layout_engine.set_text_measure(text_renderer_, text_measure_cb);
        auto layout_r = co_await layout_engine.layout_async(page.dom.get(), page.styles,
                                                     static_cast<f32>(viewport_width_),
                                                     static_cast<f32>(viewport_height_));
        if (layout_r.is_ok()) {
            page.layout = std::move(layout_r.unwrap());
        }
    }

    if (page.layout) {
        render::Painter painter(text_renderer_);
        auto paint_r = co_await painter.paint_async(page.layout.get());
        if (paint_r.is_ok()) {
            page.display_list = std::move(paint_r.unwrap());
        }
    }

    page.load_time_ms = static_cast<u32>(elapsed_ms(start));
    loaded_channel_.send(std::move(page));
    co_return;
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
                        // Route through ResourceLoader instead of direct fetch
                        html::ResourceRequest rreq;
                        rreq.url = css_url.to_string();
                        rreq.priority = html::ResourcePriority::CSS;
                        resource_loader_.request(rreq);
                    }
                }
            }
        }
    });
}

async::task<bool> PageLoader::fetch_css_content(std::string& merged_css) {
    co_await async::thread_pool_executor{};
    // resource_loader_ already has CSS URLs queued from collect_css.
    // We need to fetch those CSS files and merge their content.
    // Create a temporary loader for CSS only, or fetch them one by one.
    for (const auto& url : resource_loader_.pending_urls()) {
        auto resp = resource_loader_.fetch_single(url, html::ResourcePriority::CSS);
        if (resp.success && !resp.data.empty()) {
            std::string css_text(reinterpret_cast<const char*>(resp.data.data()), resp.data.size());
            merged_css += css_text + "\n";
        }
    }
    // Also fetch from the main list (but don't re-fetch what's already done)
    co_return true;
}

void PageLoader::collect_resources(html::Document* doc, const net::URL& base_url) {
    html::traverse_depth_first(doc, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);
        if (el->tag_name == "img") {
            auto src = el->get_attribute("src");
            if (!src.empty()) {
                auto url_r = base_url.resolve(src);
                if (url_r.is_ok()) {
                    html::ResourceRequest rreq;
                    rreq.url = url_r.unwrap().to_string();
                    rreq.priority = html::ResourcePriority::IMAGE;
                    resource_loader_.request(rreq);
                }
            }
        }
    });
}

async::task<bool> PageLoader::load_and_decode_images(const std::string&) {
    co_await async::thread_pool_executor{};
    auto resources = resource_loader_.fetch_all();
    for (auto& res : resources) {
        if (!res.success || res.data.empty()) continue;
        auto fmt = image::detect_format(res.data.data(), res.data.size());
        if (fmt == image::ImageFormat::UNKNOWN) continue;
        auto decoder = image::create_decoder(fmt);
        if (!decoder) continue;
        auto img_r = decoder->decode(res.data.data(), res.data.size());
        if (img_r.is_ok()) {
            auto img = std::make_shared<image::Image>(std::move(img_r.unwrap()));
            loaded_images_[res.url] = std::move(img);
        }
    }
    co_return true;
}

u64 PageLoader::elapsed_ms(std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

} // namespace browser
