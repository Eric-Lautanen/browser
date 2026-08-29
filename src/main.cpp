#include "../browser/browser_window.hpp"
#include "../src/crash_report.hpp"
#include "dump/json_writer.hpp"
#include "dump/dom_dump.hpp"
#include "dump/css_dump.hpp"
#include "dump/cascade_dump.hpp"
#include "dump/layout_dump.hpp"
#include "dump/display_list_dump.hpp"
#include "dump/helpers.hpp"
#include "dump/test_suite.hpp"
#include "../css/cascade.hpp"
#include "../css/layout.hpp"
#include "../css/parser.hpp"
#include "../html/parser.hpp"
#include "../html/traversal.hpp"
#include "../html/utf8.hpp"
#include "../render/font/atlas.hpp"
#include "../render/font/font.hpp"
#include "../render/paint.hpp"
#include "../render/paint/painter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using browser::f32;
using browser::u32;

int run_test_suite(const std::string &test_dir, const std::string &filter_str);

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) {
        SetProcessDPIAware();
        return run_browser("about:blank");
    }

    std::string flag = argv[1];

    if (flag == "--help" || flag == "-h") {
        std::cerr << "Usage:\n"
                  << "  browser                       Open about:blank\n"
                  << "  browser <url>                 Open a URL or file\n"
                  << "  browser --dump-dom <file>     Dump DOM tree as JSON\n"
                  << "  browser --dump-css <file>     Dump CSS AST as JSON\n"
                  << "  browser --dump-cascade <file> Dump computed styles as JSON\n"
                  << "  browser --dump-layout <file>  Dump layout tree as JSON\n"
                  << "  browser --dump-display-list <file> Dump display list as JSON\n"
                  << "  browser --screenshot <file.html | url> <out.bmp>  Render page to BMP screenshot\n"
                  << "  browser --test-suite <dir>   Run all tests in directory (single process)\n";
        return 0;
    }

    if (flag == "--screenshot") {
        if (argc < 4) {
            std::cerr << "Usage: browser --screenshot <file.html | url> <output.bmp>\n";
            return 1;
        }
        SetProcessDPIAware();
        std::string target = argv[2];
        if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0) {
            return run_browser_screenshot(target, argv[3]);
        }
        // Resolve to absolute path for file:/// URL
        if (target.find('/') == std::string::npos && target.find('\\') == std::string::npos) {
            // Relative path - prepend current dir
            char cwd[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, cwd);
            target = std::string(cwd) + "\\" + target;
        }
        return run_browser_screenshot("file:///" + target, argv[3]);
    }

    if (flag.rfind("--", 0) != 0) {
        SetProcessDPIAware();
        return run_browser(flag);
    }

    if (flag == "--test-suite") {
        if (argc < 3) {
            std::cerr << "Missing directory argument\n";
            return 1;
        }
        return run_test_suite(argv[2], (argc > 3) ? argv[3] : "");
    }

    if (argc < 3) {
        std::cerr << "Missing file argument for " << flag << std::endl;
        return 1;
    }

    std::string filepath = argv[2];
    std::ifstream f(filepath);
    if (!f.is_open()) {
        std::cerr << "Error: cannot open file: " << filepath << std::endl;
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    // --dump-dom
    if (flag == "--dump-dom") {
        auto doc_r = browser::html::parse_async(content).sync_wait();
        if (doc_r.is_err()) {
            std::cerr << "Parse error: " << doc_r.unwrap_err() << std::endl;
            return 1;
        }
        auto doc = std::move(doc_r.unwrap());
        std::cout << dump_dom_document(filepath, doc.get()) << std::endl;
        return 0;
    }

    // --dump-css
    if (flag == "--dump-css") {
        browser::css::CssParser parser(content);
        auto sheet = parser.parse();
        std::cout << dump_stylesheet(sheet) << std::endl;
        return 0;
    }

    // --dump-cascade / --dump-layout / --dump-display-list
    if (flag == "--dump-cascade" || flag == "--dump-layout" || flag == "--dump-display-list") {
        SetProcessDPIAware();
        auto doc_r = browser::html::parse_async(content).sync_wait();
        if (doc_r.is_err()) {
            std::cerr << "HTML parse error: " << doc_r.unwrap_err() << std::endl;
            return 1;
        }
        auto doc = std::move(doc_r.unwrap());

        std::string merged_css = collect_css_from_dom(doc.get());
        browser::css::StyleSheet author_sheet;
        if (!merged_css.empty()) {
            browser::css::CssParser cp(merged_css);
            author_sheet = cp.parse();
        }

        browser::css::Cascade cascader;
        auto styles_r = cascader.compute_async(*doc, author_sheet, 800, 600).sync_wait();
        if (styles_r.is_err()) {
            std::cerr << "Cascade error: " << styles_r.unwrap_err() << std::endl;
            return 1;
        }
        auto styles = std::move(styles_r.unwrap().element_styles);

        if (flag == "--dump-cascade") {
            json::Arr elements;
            browser::html::traverse_depth_first(doc.get(), [&](browser::html::Node *n) {
                if (n->type != browser::html::NodeType::ELEMENT)
                    return;
                auto *el = static_cast<browser::html::Element *>(n);
                auto it = styles.find(el);
                if (it != styles.end())
                    elements.push(dump_cascade_element(el, it->second));
            });
            json::Obj out;
            out.kv_raw("elements", elements.done());
            std::cout << out.done() << std::endl;
            return 0;
        }

        auto fs = setup_font();
        browser::css::LayoutEngine layout_engine;
        if (fs.ok) {
            layout_engine.set_text_measure(fs.tr.get(), text_measure_cb);
            layout_engine.set_text_metrics(fs.tr.get(), text_metrics_cb);
        } else {
            layout_engine.set_text_measure(nullptr, headless_text_measure);
            layout_engine.set_text_metrics(nullptr, headless_text_metrics);
        }
        auto layout_r = layout_engine.layout_async(doc.get(), styles, 800.0f, 600.0f).sync_wait();
        if (layout_r.is_err()) {
            std::cerr << "Layout error: " << layout_r.unwrap_err() << std::endl;
            return 1;
        }
        auto layout = std::move(layout_r.unwrap());

        if (flag == "--dump-layout") {
            std::cout << dump_layout_node(layout.get()) << std::endl;
            return 0;
        }

        browser::render::Painter painter(nullptr);
        auto dl_r = painter.paint_async(layout.get()).sync_wait();
        if (dl_r.is_err()) {
            std::cerr << "Paint error: " << dl_r.unwrap_err() << std::endl;
            return 1;
        }
        auto dl = std::move(dl_r.unwrap());

        json::Arr cmds;
        for (auto &cmd : dl->commands()) cmds.push(dump_command(cmd));
        std::cout << cmds.done() << std::endl;
        return 0;
    }

    // --test-suite handling moved above file-reading section
    std::cerr << "Unknown flag: " << flag << std::endl;
    return 1;
}

// ---------------------------------------------------------------------------
// --test-suite: runs all tests in a directory in a single process
// ---------------------------------------------------------------------------
