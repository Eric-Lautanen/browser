#include "page_loader.hpp"

#include "../async/executor.hpp"
#include "../css/cascade.hpp"
#include "../css/layout.hpp"
#include "../css/parser.hpp"
#include "../html/parser.hpp"
#include "../html/traversal.hpp"
#include "../image/decoder.hpp"
#include "../net/deflate.hpp"
#include "../net/http_client.hpp"
#include "../net/tracker_blocker.hpp"
#include "../net/url.hpp"
#include "../render/painter.hpp"
#include "paths.hpp"
#include "settings.hpp"
#include "telemetry.hpp"
#include "theme.hpp"

#include <fstream>

namespace browser {

    static f32 text_measure_cb(void *ctx, const std::string &text, u32 pixel_size) {
        return static_cast<render::TextRenderer *>(ctx)->measure_text(text, pixel_size);
    }

    static css::FontMetrics text_metrics_cb(void *ctx, u32 pixel_size) {
        return static_cast<render::TextRenderer *>(ctx)->get_font_metrics(pixel_size);
    }

    PageLoader::PageLoader(Telemetry *telemetry,
                           SettingsManager *settings,
                           net::TrackerBlocker *tracker,
                           render::TextRenderer *text_renderer)
        : resource_loader_(&http_),
          telemetry_(telemetry),
          settings_(settings),
          tracker_(tracker),
          text_renderer_(text_renderer),
          loaded_channel_(1) {}

    void PageLoader::start_load(const std::string &url_str) {
        if (loading_.exchange(true, std::memory_order_acq_rel))
            return;
        cancelled_.store(false, std::memory_order_release);
        load_task_ = load(url_str);
        load_task_.start();
    }

    std::optional<LoadedPage> PageLoader::try_get_loaded_page() {
        return loaded_channel_.try_receive();
    }

    static std::string html_escape(const std::string &s) {
        std::string r;
        for (char c : s) {
            switch (c) {
                case '<':
                    r += "&lt;";
                    break;
                case '>':
                    r += "&gt;";
                    break;
                case '&':
                    r += "&amp;";
                    break;
                case '"':
                    r += "&quot;";
                    break;
                default:
                    r += c;
            }
        }
        return r;
    }

    async::task<void> PageLoader::load(std::string url_str) {
        co_await async::thread_pool_executor{};
        auto start = std::chrono::steady_clock::now();

        if (cancelled_.load(std::memory_order_acquire)) {
            loading_.store(false, std::memory_order_release);
            co_return;
        }

        if (url_str.rfind("view-source:", 0) == 0) {
            std::string inner_url = url_str.substr(12);
            // Fetch the inner URL as plain text
            std::string normal_url = inner_url;
            if (inner_url.find("://") == std::string::npos && inner_url.rfind("about:", 0) != 0 &&
                inner_url.rfind("file:", 0) != 0) {
                normal_url = "http://" + inner_url;
            }
            auto parsed = net::URL::parse(normal_url);
            if (parsed.is_err()) {
                co_await load_html(error_page(url_str, "Invalid URL: " + parsed.unwrap_err()));
                loading_.store(false, std::memory_order_release);
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
            req.headers.set("Accept", "text/html,application/xhtml+xml,text/plain");
            req.headers.set("Accept-Encoding", "gzip, deflate");

            auto resp_r = co_await http_.fetch_async(req);
            if (cancelled_.load(std::memory_order_acquire)) {
                loading_.store(false, std::memory_order_release);
                co_return;
            }
            if (resp_r.is_err()) {
                co_await load_html(error_page(url_str, resp_r.unwrap_err()));
                loading_.store(false, std::memory_order_release);
                co_return;
            }
            auto resp = std::move(resp_r.unwrap());

            if (resp.headers.has("content-encoding")) {
                co_await async::thread_pool_executor{};
                if (cancelled_.load(std::memory_order_acquire)) {
                    loading_.store(false, std::memory_order_release);
                    co_return;
                }
                std::string ce = resp.headers.get("content-encoding");
                if (ce.find("gzip") != std::string::npos) {
                    resp.body = net::gzip_decompress(resp.body.data(), static_cast<u32>(resp.body.size()));
                } else if (ce.find("deflate") != std::string::npos) {
                    resp.body = net::inflate(resp.body.data(), static_cast<u32>(resp.body.size()));
                }
            }

            // Build syntax-highlighted HTML source
            std::string raw_body(reinterpret_cast<const char *>(resp.body.data()), resp.body.size());
            std::string escaped = html_escape(raw_body);
            std::string html =
                "<!DOCTYPE html><html><head><style>"
                "body{background:#1e1e1e;color:#d4d4d4;font-family:monospace;padding:16px;font-size:13px;white-space:"
                "pre}"
                ".tag{color:#569cd6}.attr{color:#9cdcfe}.str{color:#ce9178}.cmt{color:#6a9955}"
                "</style></head><body><pre><code>";
            // Simple syntax highlighting: wrap tags, attributes, strings, comments
            std::string highlighted;
            for (size_t i = 0; i < escaped.size();) {
                if (escaped.substr(i, 4) == "&lt;" && i + 4 < escaped.size()) {
                    // Check for comment
                    if (escaped.substr(i + 4, 3) == "!--") {
                        auto end = escaped.find("--&gt;", i);
                        if (end == std::string::npos)
                            end = escaped.size();
                        else
                            end += 6;
                        highlighted += "<span class=\"cmt\">" + escaped.substr(i, end - i) + "</span>";
                        i = end;
                        continue;
                    }
                    // Tag
                    auto end = escaped.find("&gt;", i);
                    if (end != std::string::npos)
                        end += 4;
                    else
                        end = escaped.size();
                    std::string tag_content = escaped.substr(i, end - i);
                    // Highlight attributes within tag
                    std::string colored_tag;
                    std::string remaining = tag_content;
                    // Remove the outer tag markers for processing
                    colored_tag += "&lt;";
                    remaining = remaining.substr(4);
                    if (!remaining.empty() && remaining.back() == ';' && remaining.size() >= 4 &&
                        remaining.substr(remaining.size() - 4) == "&gt;") {
                        // Process tag content
                        std::string inner = remaining.substr(0, remaining.size() - 4);
                        // Wrap tag name in tag color
                        auto space = inner.find(' ');
                        if (space == std::string::npos) {
                            colored_tag += "<span class=\"tag\">" + inner + "</span>";
                        } else {
                            colored_tag += "<span class=\"tag\">" + inner.substr(0, space) + "</span>";
                            inner = inner.substr(space);
                            // Attributes
                            size_t pos = 0;
                            while (pos < inner.size()) {
                                // Skip whitespace
                                while (pos < inner.size() && inner[pos] == ' ') {
                                    colored_tag += ' ';
                                    pos++;
                                }
                                if (pos >= inner.size())
                                    break;
                                auto eq = inner.find('=', pos);
                                if (eq == std::string::npos) {
                                    colored_tag += "<span class=\"attr\">" + inner.substr(pos) + "</span>";
                                    break;
                                }
                                std::string attr_name = inner.substr(pos, eq - pos);
                                colored_tag += "<span class=\"attr\">" + attr_name + "</span>";
                                colored_tag += '=';
                                pos = eq + 1;
                                if (pos < inner.size() && (inner[pos] == '"' || inner[pos] == '\'')) {
                                    char quote = inner[pos];
                                    auto end_q = inner.find(quote, pos + 1);
                                    if (end_q == std::string::npos) {
                                        colored_tag += "<span class=\"str\">" + inner.substr(pos) + "</span>";
                                        break;
                                    }
                                    colored_tag +=
                                        "<span class=\"str\">" + inner.substr(pos, end_q - pos + 1) + "</span>";
                                    pos = end_q + 1;
                                }
                            }
                        }
                        colored_tag += "&gt;";
                    } else {
                        colored_tag += tag_content.substr(4);
                    }
                    highlighted += colored_tag;
                    i = end;
                } else {
                    highlighted += escaped[i];
                    i++;
                }
            }
            html += highlighted;
            html += "</code></pre></body></html>";
            co_await load_html(html);
            loading_.store(false, std::memory_order_release);
            co_return;
        }

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
            loading_.store(false, std::memory_order_release);
            co_return;
        }

        if (url_str.rfind("file:///", 0) == 0) {
            std::string path = url_str.substr(8);
            std::ifstream f(path);
            if (!f.is_open()) {
                co_await load_html(error_page(url_str, "Cannot open file: " + path));
                loading_.store(false, std::memory_order_release);
                co_return;
            }
            std::string html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            co_await load_html(html);
            loading_.store(false, std::memory_order_release);
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
            loading_.store(false, std::memory_order_release);
            co_return;
        }

        if (tracker_ && tracker_->should_block(url_str)) {
            telemetry_->record({TelemetryEvent::TRACKER_BLOCKED, url_str, 0});
            telemetry_->set_trackers_blocked(tracker_->blocked_count());
            co_await load_html(error_page(url_str, "Blocked by tracker blocker"));
            loading_.store(false, std::memory_order_release);
            co_return;
        }

        auto &hsts = net::HTTPClient::hsts_manager();
        hsts.load_preload_list();
        {
            std::string upgraded = hsts.upgrade_url(parsed.unwrap().to_string());
            if (upgraded != parsed.unwrap().to_string()) {
                auto new_parsed = net::URL::parse(upgraded);
                if (new_parsed.is_ok()) {
                    parsed = std::move(new_parsed);
                }
            }
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
        if (cancelled_.load(std::memory_order_acquire)) {
            loading_.store(false, std::memory_order_release);
            co_return;
        }
        if (resp_r.is_err()) {
            co_await load_html(error_page(url_str, resp_r.unwrap_err()));
            loading_.store(false, std::memory_order_release);
            co_return;
        }
        auto resp = std::move(resp_r.unwrap());

        u32 redirect_count = 0;
        while ((resp.status.code == 301 || resp.status.code == 302 || resp.status.code == 303 ||
                resp.status.code == 307 || resp.status.code == 308) &&
               redirect_count < 5) {
            if (cancelled_.load(std::memory_order_acquire)) {
                loading_.store(false, std::memory_order_release);
                co_return;
            }
            std::string loc = resp.headers.get("Location");
            if (loc.empty())
                break;
            redirect_count++;
            auto new_url = req.url.resolve(loc);
            if (new_url.is_err())
                break;
            req.url = new_url.unwrap();
            {
                std::string host_hdr = req.url.host;
                if (req.url.port != 0 && req.url.port != req.url.default_port())
                    host_hdr += ":" + std::to_string(req.url.port);
                req.headers.set("Host", host_hdr);
            }
            auto nr = co_await http_.fetch_async(req);
            if (nr.is_err())
                break;
            resp = std::move(nr.unwrap());
        }

        // Check for Content-Disposition: attachment on the final response (after redirects)
        {
            std::string cd = resp.headers.get("Content-Disposition");
            if (!cd.empty() && cd.find("attachment") != std::string::npos) {
                std::string mime_type = resp.headers.get("Content-Type");
                u64 content_length = 0;
                std::string cl = resp.headers.get("Content-Length");
                if (!cl.empty()) {
                    char *end = nullptr;
                    u64 parsed = std::strtoull(cl.c_str(), &end, 10);
                    if (end && end != cl.c_str())
                        content_length = parsed;
                }
                if (download_callback_ && download_callback_(req.url.to_string(), cd, mime_type, content_length)) {
                    loading_.store(false, std::memory_order_release);
                    co_return;
                }
            }
        }

        page_is_https_ = (req.url.scheme == "https");
        has_mixed_content_ = false;
        current_csp_ = net::CSPPolicy();
        {
            std::string csp_val;
            std::string hsts_val;
            for (auto &[hk, hv] : resp.headers.all()) {
                std::string lk;
                for (char ch : hk) lk += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lk == "content-security-policy") {
                    csp_val = hv;
                } else if (lk == "strict-transport-security") {
                    hsts_val = hv;
                }
            }
            if (!csp_val.empty()) {
                current_csp_ = net::CSPParser::parse(csp_val);
            }
            if (!hsts_val.empty()) {
                hsts.process_header(req.url.host, hsts_val);
            }
        }

        if (resp.headers.has("content-encoding")) {
            co_await async::thread_pool_executor{};
            if (cancelled_.load(std::memory_order_acquire)) {
                loading_.store(false, std::memory_order_release);
                co_return;
            }
            std::string ce = resp.headers.get("content-encoding");
            if (ce.find("gzip") != std::string::npos) {
                resp.body = net::gzip_decompress(resp.body.data(), static_cast<u32>(resp.body.size()));
            } else if (ce.find("deflate") != std::string::npos) {
                resp.body = net::inflate(resp.body.data(), static_cast<u32>(resp.body.size()));
            }
        }

        std::string body_str(reinterpret_cast<const char *>(resp.body.data()), resp.body.size());

        std::string base_url_str = req.url.to_string();
        loaded_images_.clear();
        resource_loader_ = html::ResourceLoader(&http_);

        preload_scanner_ = html::PreloadScanner();
        preload_scanner_.set_fetch_callback([&](const html::PreloadRequest &preq) {
            html::ResourcePriority prio = html::ResourcePriority::IMAGE;
            if (preq.as == "style")
                prio = html::ResourcePriority::CSS;
            else if (preq.as == "script")
                prio = html::ResourcePriority::JS;
            resource_loader_.request({preq.url, prio, preq.is_async, preq.is_defer, preq.is_module});
        });

        auto doc_r = co_await html::parse_async(body_str, &preload_scanner_, base_url_str);
        if (cancelled_.load(std::memory_order_acquire)) {
            loading_.store(false, std::memory_order_release);
            co_return;
        }
        if (doc_r.is_err()) {
            loading_.store(false, std::memory_order_release);
            co_return;
        }

        LoadedPage page;
        page.dom = std::move(doc_r.unwrap());
        auto *title_el = html::find_element_by_tag(page.dom.get(), "title");
        if (title_el)
            page.page_title = html::inner_text(title_el);

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

        // Load and decode images
        collect_resources(page.dom.get(), req.url);
        co_await load_and_decode_images(base_url_str);
        page.images = loaded_images_;

        if (page.dom) {
            css::LayoutEngine layout_engine;
            layout_engine.set_text_measure(text_renderer_, text_measure_cb);
            layout_engine.set_text_metrics(text_renderer_, text_metrics_cb);
            auto layout_r = co_await layout_engine.layout_async(
                page.dom.get(), page.styles, static_cast<f32>(viewport_width_), static_cast<f32>(viewport_height_));
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

        if (!cancelled_.load(std::memory_order_acquire)) {
            loaded_channel_.send(std::move(page));
        }
        loading_.store(false, std::memory_order_release);
        co_return;
    }

    async::task<void> PageLoader::load_html(std::string html) {
        co_await async::thread_pool_executor{};
        if (cancelled_.load(std::memory_order_acquire)) {
            loading_.store(false, std::memory_order_release);
            co_return;
        }
        auto start = std::chrono::steady_clock::now();

        auto doc_r = co_await browser::html::parse_async(html);
        if (doc_r.is_err()) {
            loaded_channel_.send(LoadedPage{});
            co_return;
        }

        LoadedPage page;
        page.dom = std::move(doc_r.unwrap());
        auto *title_el = html::find_element_by_tag(page.dom.get(), "title");
        if (title_el)
            page.page_title = html::inner_text(title_el);

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
            layout_engine.set_text_metrics(text_renderer_, text_metrics_cb);
            auto layout_r = co_await layout_engine.layout_async(
                page.dom.get(), page.styles, static_cast<f32>(viewport_width_), static_cast<f32>(viewport_height_));
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
        if (!cancelled_.load(std::memory_order_acquire)) {
            loaded_channel_.send(std::move(page));
        }
        loading_.store(false, std::memory_order_release);
        co_return;
    }

    void PageLoader::cancel() {
        cancelled_.store(true, std::memory_order_release);
        loading_.store(false, std::memory_order_release);
    }

    bool PageLoader::is_loading() const {
        return loading_.load(std::memory_order_acquire);
    }

    void PageLoader::handle_settings_query(const std::string &url_str) {
        auto qpos = url_str.find('?');
        if (qpos == std::string::npos)
            return;
        auto query = url_str.substr(qpos + 1);
        auto eq = query.find('=');
        if (eq == std::string::npos)
            return;
        auto key = query.substr(0, eq);
        auto val = net::url_decode(query.substr(eq + 1));
        if (key == "theme") {
            settings_->set_theme(val == "dark" ? ThemeMode::DARK : ThemeMode::LIGHT);
        } else if (key == "homepage") {
            settings_->set_homepage(val);
        } else if (key == "search") {
            settings_->set_search_engine(val);
        } else if (key == "download") {
            settings_->set_download_behavior(val);
        } else if (key == "cookie_policy") {
            settings_->set_cookie_policy(val);
        } else if (key == "font_size") {
            settings_->set_font_size(static_cast<u32>(std::stoul(val)));
        } else if (key == "zoom") {
            settings_->set_zoom_level(std::stof(val));
        }
        settings_->save_to_file(data_dir() + "/settings.txt");
    }

    std::string PageLoader::error_page(const std::string &url, const std::string &msg) {
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
            if (error_msg.empty())
                error_msg = "Unknown error";
        }
        return "<html><body style='font-family:sans-serif;padding:2em'>"
               "<h2>Error loading page</h2>"
               "<p><b>URL:</b> " +
               html_escape(url) +
               "</p>"
               "<p><b>Reason:</b> " +
               html_escape(error_msg) +
               "</p>"
               "</body></html>";
    }

    void PageLoader::collect_css(html::Document *doc, std::string &merged_css, const net::URL &base_url) {
        html::traverse_depth_first(doc, [&](html::Node *node) {
            if (node->type != html::NodeType::ELEMENT)
                return;
            auto *el = static_cast<html::Element *>(node);
            if (el->tag_name == "style") {
                if (current_csp_.has_directive("style-src") || current_csp_.has_directive("default-src")) {
                    if (!current_csp_.allows_inline_style()) {
                        return;
                    }
                }
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
                            std::string css_url_str = css_url.to_string();
                            if (page_is_https_) {
                                if (css_url.scheme == "http" && css_url.scheme != "data" && css_url.scheme != "blob" &&
                                    css_url.scheme != "about") {
                                    has_mixed_content_ = true;
                                    return;
                                }
                            }
                            if (current_csp_.has_directive("style-src") || current_csp_.has_directive("default-src")) {
                                if (!current_csp_.allows("style-src", css_url_str) &&
                                    !current_csp_.allows("default-src", css_url_str)) {
                                    return;
                                }
                            }
                            html::ResourceRequest rreq;
                            rreq.url = css_url_str;
                            rreq.priority = html::ResourcePriority::CSS;
                            resource_loader_.request(rreq);
                        }
                    }
                }
            }
        });
    }

    async::task<bool> PageLoader::fetch_css_content(std::string &merged_css) {
        co_await async::thread_pool_executor{};
        // resource_loader_ already has CSS URLs queued from collect_css.
        // We need to fetch those CSS files and merge their content.
        // Create a temporary loader for CSS only, or fetch them one by one.
        for (const auto &url : resource_loader_.pending_urls()) {
            auto resp = resource_loader_.fetch_single(url, html::ResourcePriority::CSS);
            if (resp.success && !resp.data.empty()) {
                std::string css_text(reinterpret_cast<const char *>(resp.data.data()), resp.data.size());
                merged_css += css_text + "\n";
            }
        }
        // Also fetch from the main list (but don't re-fetch what's already done)
        co_return true;
    }

    void PageLoader::collect_resources(html::Document *doc, const net::URL &base_url) {
        html::traverse_depth_first(doc, [&](html::Node *node) {
            if (node->type != html::NodeType::ELEMENT)
                return;
            auto *el = static_cast<html::Element *>(node);
            if (el->tag_name == "img") {
                auto src = el->get_attribute("src");
                if (!src.empty()) {
                    auto url_r = base_url.resolve(src);
                    if (url_r.is_ok()) {
                        auto img_url = url_r.unwrap();
                        std::string img_url_str = img_url.to_string();
                        if (img_url.scheme != "data" && img_url.scheme != "blob" && img_url.scheme != "about") {
                            if (page_is_https_ && img_url.scheme == "http") {
                                has_mixed_content_ = true;
                                return;
                            }
                            if (current_csp_.has_directive("img-src") || current_csp_.has_directive("default-src")) {
                                if (!current_csp_.allows("img-src", img_url_str) &&
                                    !current_csp_.allows("default-src", img_url_str)) {
                                    return;
                                }
                            }
                        }
                        html::ResourceRequest rreq;
                        rreq.url = img_url_str;
                        rreq.priority = html::ResourcePriority::IMAGE;
                        resource_loader_.request(rreq);
                    }
                }
            }
        });
    }

    async::task<bool> PageLoader::load_and_decode_images(const std::string &) {
        co_await async::thread_pool_executor{};
        auto resources = resource_loader_.fetch_all();
        for (auto &res : resources) {
            if (!res.success || res.data.empty())
                continue;
            auto fmt = image::detect_format(res.data.data(), res.data.size());
            if (fmt == image::ImageFormat::UNKNOWN)
                continue;
            auto decoder = image::create_decoder(fmt);
            if (!decoder)
                continue;
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

}  // namespace browser
