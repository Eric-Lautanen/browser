#include "helpers.hpp"
#include "json_writer.hpp"
#include "../../core/utility.hpp"
#include "../../html/traversal.hpp"
#include "../../render/font/font.hpp"
#include "../../render/font/atlas.hpp"
#include "../../css/layout.hpp"
#include "../../browser/browser_window.hpp"
#include "../crash_report.hpp"
#include <iostream>

using browser::f32;
using browser::u32;

// ---------------------------------------------------------------------------
// Text measurer
// ---------------------------------------------------------------------------
f32 headless_text_measure(void *, const std::string &text, u32 pixel_size) {
    return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
}
browser::css::FontMetrics headless_text_metrics(void *, u32 pixel_size) {
    browser::css::FontMetrics fm = {};
    fm.ascender = (f32)pixel_size * 0.8f;
    fm.descender = (f32)pixel_size * -0.2f;
    return fm;
}

f32 text_measure_cb(void *ctx, const std::string &text, u32 pixel_size) {
    return static_cast<browser::render::TextRenderer *>(ctx)->measure_text(text, pixel_size);
}
browser::css::FontMetrics text_metrics_cb(void *ctx, u32 pixel_size) {
    return static_cast<browser::render::TextRenderer *>(ctx)->get_font_metrics(pixel_size);
}

FontSetup setup_font() {
    FontSetup fs;
    fs.fm = std::make_unique<browser::render::FontManager>();
    fs.tr = std::make_unique<browser::render::TextRenderer>();
    auto r = fs.fm->load_default_font();
    if (r.is_ok()) {
        fs.tr->set_font_face(r.unwrap(), fs.fm.get());
        fs.ok = true;
    }
    return fs;
}

// ---------------------------------------------------------------------------
// Collect inline CSS from <style> tags
// ---------------------------------------------------------------------------
std::string collect_css_from_dom(browser::html::Node *node) {
    std::string css;
    browser::html::traverse_depth_first(node, [&](browser::html::Node *n) {
        if (n->type == browser::html::NodeType::ELEMENT) {
            auto *el = static_cast<browser::html::Element *>(n);
            if (el->tag_name == "style") {
                for (auto &ch : n->children) {
                    if (ch->type == browser::html::NodeType::TEXT) {
                        css += static_cast<browser::html::Text *>(ch.get())->data + "\n";
                    }
                }
            }
        }
    });
    return css;
}

// ---------------------------------------------------------------------------
// Normal browser mode
// ---------------------------------------------------------------------------
int run_browser(const std::string &url) {
    ::browser::install_crash_reporter("browser");
    browser::BrowserWindow browser;
    auto r = browser.initialize();
    if (r.is_err()) {
        std::cerr << "Failed to initialize: " << r.unwrap_err() << std::endl;
        return 1;
    }
    browser.navigate(url);
    browser.run();
    return 0;
}

int run_browser_screenshot(const std::string &filepath, const std::string &outpath) {
    browser::BrowserWindow browser;
    auto r = browser.initialize();
    if (r.is_err()) {
        std::cerr << "Failed to initialize: " << r.unwrap_err() << std::endl;
        return 1;
    }
    browser.navigate(filepath);
    browser.run_with_screenshot(outpath);
    return 0;
}

