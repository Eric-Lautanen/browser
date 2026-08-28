#pragma once
#include "../../core/utility.hpp"
#include "../../html/dom.hpp"
#include "../../css/layout.hpp"
#include "../../render/font/font.hpp"
#include "../../render/font/atlas.hpp"
#include <string>
#include <memory>

browser::f32 headless_text_measure(void *, const std::string &text, browser::u32 pixel_size);
browser::css::FontMetrics headless_text_metrics(void *, browser::u32 pixel_size);
browser::f32 text_measure_cb(void *ctx, const std::string &text, browser::u32 pixel_size);
browser::css::FontMetrics text_metrics_cb(void *ctx, browser::u32 pixel_size);
struct FontSetup { std::unique_ptr<browser::render::FontManager> fm; std::unique_ptr<browser::render::TextRenderer> tr; bool ok=false; };
FontSetup setup_font();
std::string collect_css_from_dom(browser::html::Node *node);
int run_browser(const std::string &url);
int run_browser_screenshot(const std::string &filepath, const std::string &outpath);
