#include "../browser/browser_window.hpp"
#include "../src/crash_report.hpp"
#include "dump/json_writer.hpp"
#include "dump/dom_dump.hpp"
#include "dump/css_dump.hpp"
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

// ---------------------------------------------------------------------------
// Cascade dump
// ---------------------------------------------------------------------------
static std::string css_value_to_json(const browser::css::CSSValue &v) {
    json::Obj o;
    o.kv_raw("type", json::q(css_val_type_str(v.type)));
    switch (v.type) {
        case browser::css::CSSValue::Type::KEYWORD:
            o.kv_raw("value", json::q(v.keyword));
            break;
        case browser::css::CSSValue::Type::LENGTH:
            o.kv_num("value", v.length.value);
            {
                std::string u;
                switch (v.length.unit) {
                    case browser::css::Length::Unit::PX:
                        u = "px";
                        break;
                    case browser::css::Length::Unit::EM:
                        u = "em";
                        break;
                    case browser::css::Length::Unit::REM:
                        u = "rem";
                        break;
                    case browser::css::Length::Unit::PERCENT:
                        u = "%";
                        break;
                    case browser::css::Length::Unit::VW:
                        u = "vw";
                        break;
                    case browser::css::Length::Unit::VH:
                        u = "vh";
                        break;
                    case browser::css::Length::Unit::NONE:
                        u = "";
                        break;
                    default:
                        u = "px";
                        break;
                }
                o.kv_raw("unit", json::q(u));
            }
            break;
        case browser::css::CSSValue::Type::COLOR:
            o.kv_num("r", static_cast<f32>(v.color.r));
            o.kv_num("g", static_cast<f32>(v.color.g));
            o.kv_num("b", static_cast<f32>(v.color.b));
            o.kv_num("a", static_cast<f32>(v.color.a));
            break;
        case browser::css::CSSValue::Type::NUMBER:
        case browser::css::CSSValue::Type::PERCENTAGE:
            o.kv_num("value", v.number);
            break;
        case browser::css::CSSValue::Type::STRING:
        case browser::css::CSSValue::Type::URL:
            o.kv_raw("value", json::q(v.string_value));
            break;
        default:
            o.kv_raw("value", json::q(v.keyword));
            break;
    }
    return o.done();
}

static std::string dump_cascade_element(const browser::html::Element *el, const browser::css::ComputedStyle &style) {
    json::Obj o;
    o.kv_raw("tag", json::q(el->tag_name));
    o.kv_raw("id", json::q(el->id()));
    json::Arr cls;
    for (auto &c : el->class_list()) cls.push(json::q(c));
    o.kv_raw("classes", cls.done());
    json::Obj props;
    for (auto &[prop, val] : style.properties) {
        if (prop.size() > 1 && prop[0] == '_')
            continue;
        props.kv_raw(prop, css_value_to_json(val));
    }
    o.kv_raw("computed", props.done());
    return o.done();
}

// ---------------------------------------------------------------------------
// Layout dump
// ---------------------------------------------------------------------------
static std::string edge_json(const browser::css::EdgeSizes &e) {
    json::Obj r;
    r.kv_num("top", e.top);
    r.kv_num("right", e.right);
    r.kv_num("bottom", e.bottom);
    r.kv_num("left", e.left);
    return r.done();
}

static std::string dump_layout_node(const browser::css::LayoutNode *node) {
    if (!node)
        return "null";
    json::Obj o;
    if (!node->is_text()) {
        auto *n = node->node();
        if (n && n->type == browser::html::NodeType::ELEMENT)
            o.kv_raw("tag", json::q(static_cast<browser::html::Element *>(n)->tag_name));
        else
            o.kv("tag", "(anonymous)");
    } else {
        o.kv("tag", "(text)");
    }
    o.kv_bool("is_text", node->is_text());
    o.kv_raw("text", json::q(node->text()));
    json::Obj cr;
    cr.kv_num("x", node->content.x);
    cr.kv_num("y", node->content.y);
    cr.kv_num("width", node->content.width);
    cr.kv_num("height", node->content.height);
    o.kv_raw("content", cr.done());
    o.kv_raw("margin", edge_json(node->margin));
    o.kv_raw("padding", edge_json(node->padding));
    o.kv_raw("border", edge_json(node->border));
    if (!node->text_lines.empty()) {
        json::Arr lines;
        for (auto &li : node->text_lines) {
            json::Obj l;
            l.kv_num("y", li.y);
            l.kv_raw("text", json::q(li.text));
            lines.push(l.done());
        }
        o.kv_raw("text_lines", lines.done());
    }
    json::Arr kids;
    for (auto &ch : node->children) kids.push(dump_layout_node(ch.get()));
    o.kv_raw("children", kids.done());
    return o.done();
}

// ---------------------------------------------------------------------------
// Display list dump
// ---------------------------------------------------------------------------
static std::string paint_cmd_type_str(browser::render::PaintCommand::Type t) {
    switch (t) {
        case browser::render::PaintCommand::Type::FILL_RECT:
            return "FILL_RECT";
        case browser::render::PaintCommand::Type::DRAW_TEXT:
            return "DRAW_TEXT";
        case browser::render::PaintCommand::Type::PUSH_CLIP:
            return "PUSH_CLIP";
        case browser::render::PaintCommand::Type::POP_CLIP:
            return "POP_CLIP";
        case browser::render::PaintCommand::Type::DRAW_IMAGE:
            return "DRAW_IMAGE";
        case browser::render::PaintCommand::Type::DRAW_GRADIENT:
            return "DRAW_GRADIENT";
        case browser::render::PaintCommand::Type::DRAW_SHADOW:
            return "DRAW_SHADOW";
        case browser::render::PaintCommand::Type::PUSH_TRANSFORM:
            return "PUSH_TRANSFORM";
        case browser::render::PaintCommand::Type::POP_TRANSFORM:
            return "POP_TRANSFORM";
        case browser::render::PaintCommand::Type::PUSH_OPACITY:
            return "PUSH_OPACITY";
        case browser::render::PaintCommand::Type::POP_OPACITY:
            return "POP_OPACITY";
        case browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT:
            return "DRAW_ROUNDED_RECT";
        case browser::render::PaintCommand::Type::DRAW_CANVAS:
            return "DRAW_CANVAS";
        case browser::render::PaintCommand::Type::PUSH_FILTER:
            return "PUSH_FILTER";
        case browser::render::PaintCommand::Type::POP_FILTER:
            return "POP_FILTER";
    }
    return "UNKNOWN";
}

static std::string render_color_to_hex(const browser::render::Color &c) {
    char buf[16];
    snprintf(buf,
             sizeof buf,
             "#%02x%02x%02x",
             static_cast<int>(c.r * 255 + 0.5f),
             static_cast<int>(c.g * 255 + 0.5f),
             static_cast<int>(c.b * 255 + 0.5f));
    return buf;
}

static std::string dump_command(const browser::render::PaintCommand &cmd) {
    json::Obj o;
    o.kv_raw("cmd", json::q(paint_cmd_type_str(cmd.type)));
    o.kv_num("x", cmd.rect.x);
    o.kv_num("y", cmd.rect.y);
    o.kv_num("w", cmd.rect.width);
    o.kv_num("h", cmd.rect.height);
    if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_TEXT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_SHADOW) {
        o.kv_raw("color", json::q(render_color_to_hex(cmd.color)));
    }
    if (cmd.type == browser::render::PaintCommand::Type::DRAW_TEXT) {
        o.kv_raw("text", json::q(cmd.text));
        o.kv_num("font_size", cmd.font_size);
        if (cmd.font_flags)
            o.kv_num("font_flags", static_cast<f32>(cmd.font_flags));
    }
    if (cmd.radius > 0)
        o.kv_num("radius", cmd.radius);
    return o.done();
}

// ---------------------------------------------------------------------------
// Text measurer
// ---------------------------------------------------------------------------
static f32 headless_text_measure(void *, const std::string &text, u32 pixel_size) {
    return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
}
static browser::css::FontMetrics headless_text_metrics(void *, u32 pixel_size) {
    browser::css::FontMetrics fm = {};
    fm.ascender = (f32)pixel_size * 0.8f;
    fm.descender = (f32)pixel_size * -0.2f;
    return fm;
}

static f32 text_measure_cb(void *ctx, const std::string &text, u32 pixel_size) {
    return static_cast<browser::render::TextRenderer *>(ctx)->measure_text(text, pixel_size);
}
static browser::css::FontMetrics text_metrics_cb(void *ctx, u32 pixel_size) {
    return static_cast<browser::render::TextRenderer *>(ctx)->get_font_metrics(pixel_size);
}

struct FontSetup {
    std::unique_ptr<browser::render::FontManager> fm;
    std::unique_ptr<browser::render::TextRenderer> tr;
    bool ok = false;
};

static FontSetup setup_font() {
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
static std::string collect_css_from_dom(browser::html::Node *node) {
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
static int run_test_suite(const std::string &test_dir, const std::string &filter_str);

static int run_browser(const std::string &url) {
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

static int run_browser_screenshot(const std::string &filepath, const std::string &outpath) {
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
                  << "  browser --screenshot <file.html> <out.bmp>  Render HTML to BMP screenshot\n"
                  << "  browser --test-suite <dir>   Run all tests in directory (single process)\n";
        return 0;
    }

    if (flag == "--screenshot") {
        if (argc < 4) {
            std::cerr << "Usage: browser --screenshot <file.html> <output.bmp>\n";
            return 1;
        }
        SetProcessDPIAware();
        // Resolve to absolute path for file:/// URL
        std::string html_path = argv[2];
        if (html_path.find('/') == std::string::npos && html_path.find('\\') == std::string::npos) {
            // Relative path - prepend current dir
            char cwd[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, cwd);
            html_path = std::string(cwd) + "\\" + html_path;
        }
        return run_browser_screenshot("file:///" + html_path, argv[3]);
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
static int run_test_suite(const std::string &test_dir, const std::string &filter_str) {
    SetProcessDPIAware();
    int total = 0, passed_total = 0, failed_total = 0, critical_total = 0;

    // Use Win32 FindFirstFile/FindNextFile (no exceptions in this project)
    auto find_files =
        [](const std::string &dir, const std::string &ext, const std::string &flt) -> std::vector<std::string> {
        // Split filter on '|' for OR matching
        std::vector<std::string> filters;
        if (!flt.empty()) {
            size_t start = 0, pos;
            do {
                pos = flt.find('|', start);
                filters.push_back(flt.substr(start, pos - start));
                start = pos + 1;
            } while (pos != std::string::npos);
        }
        auto matches = [&](const std::string &name) -> bool {
            if (filters.empty())
                return true;
            for (auto &f : filters) {
                if (name.find(f) != std::string::npos)
                    return true;
            }
            return false;
        };
        std::vector<std::string> out;
        std::string pattern = dir + "\\*" + ext;
        WIN32_FIND_DATAA ffd;
        HANDLE h = FindFirstFileA(pattern.c_str(), &ffd);
        if (h == INVALID_HANDLE_VALUE)
            return out;
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string name = ffd.cFileName;
                if (matches(name))
                    out.push_back(dir + "\\" + name);
            }
        } while (FindNextFileA(h, &ffd));
        FindClose(h);
        std::sort(out.begin(), out.end());
        return out;
    };

    auto html_files = find_files(test_dir, ".html", filter_str);
    auto css_files = find_files(test_dir, ".css", filter_str);

    auto read_file = [](const std::string &p) -> std::string {
        std::ifstream f(p, std::ios::binary);
        if (!f.is_open())
            return "";
        std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // Strip UTF-8 BOM if present
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
            (unsigned char)raw[2] == 0xBF)
            raw = raw.substr(3);
        // Detect UTF-16 LE BOM and convert to UTF-8
        if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
            std::string out;
            out.reserve(raw.size() / 2);
            for (size_t i = 2; i + 1 < raw.size(); i += 2) {
                char16_t cp = (unsigned char)raw[i] | ((unsigned char)raw[i + 1] << 8);
                if (cp < 0x80) {
                    out += (char)cp;
                } else if (cp < 0x800) {
                    out += (char)(0xC0 | (cp >> 6));
                    out += (char)(0x80 | (cp & 0x3F));
                } else {
                    out += (char)(0xE0 | (cp >> 12));
                    out += (char)(0x80 | ((cp >> 6) & 0x3F));
                    out += (char)(0x80 | (cp & 0x3F));
                }
            }
            return out;
        }
        return raw;
    };
    auto file_exists = [](const std::string &p) -> bool {
        DWORD attr = GetFileAttributesA(p.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    };
    auto strip_path = [](const std::string &p) -> std::string {
        auto pos = p.find_last_of("/\\");
        return (pos != std::string::npos) ? p.substr(pos + 1) : p;
    };
    auto fs = setup_font();
    for (auto &html_file : html_files) {
        auto base = strip_path(html_file);
        auto stem = html_file.substr(0, html_file.size() - 5);
        std::string content = read_file(html_file);
        if (content.empty()) {
            failed_total++;
            total++;
            continue;
        }

        std::string dom_s = "SKIP";
        std::string expected_dom_path = stem + ".expected-dom.json";
        if (file_exists(expected_dom_path)) {
            auto doc_r = browser::html::parse_async(content).sync_wait();
            if (doc_r.is_err()) {
                dom_s = "ERR";
                critical_total++;
            } else {
                auto doc = std::move(doc_r.unwrap());
                // Use basename as source path for consistent cross-platform comparison
                std::string actual_str = dump_dom_document(base, doc.get());
                std::string expected_str = read_file(expected_dom_path);
                // Normalize: strip trailing newline from expected (reference adds \n via console.log)
                if (!expected_str.empty() && expected_str.back() == '\n')
                    expected_str.pop_back();
                if (!expected_str.empty() && expected_str.back() == '\r')
                    expected_str.pop_back();
                // Normalize: strip \r so CRLF vs LF vs CRCRLF differences don't cause false failures
                auto strip_cr = [](std::string &s) { s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); };
                strip_cr(actual_str);
                strip_cr(expected_str);
                dom_s = (actual_str == expected_str) ? "PASS" : "FAIL";
                if (dom_s == "FAIL") {
                    critical_total++;
                }
            }
        }

        std::string cascade_s = "SKIP", layout_s = "SKIP", disp_s = "SKIP";
        auto doc_r = browser::html::parse_async(content).sync_wait();
        if (doc_r.is_ok()) {
            auto doc = std::move(doc_r.unwrap());
            std::string merged_css;
            browser::html::traverse_depth_first(doc.get(), [&](browser::html::Node *n) {
                if (n->type == browser::html::NodeType::ELEMENT) {
                    auto *el = static_cast<browser::html::Element *>(n);
                    if (el->tag_name == "style") {
                        for (auto &ch : n->children) {
                            if (ch->type == browser::html::NodeType::TEXT)
                                merged_css += static_cast<browser::html::Text *>(ch.get())->data + "\n";
                        }
                    }
                }
            });
            browser::css::StyleSheet author_sheet;
            if (!merged_css.empty()) {
                browser::css::CssParser cp(merged_css);
                author_sheet = cp.parse();
            }
            browser::css::Cascade cascader;
            auto styles_r = cascader.compute_async(*doc, author_sheet, 800, 600).sync_wait();
            if (styles_r.is_ok()) {
                auto styles = std::move(styles_r.unwrap().element_styles);
                std::string expected_cascade_path = stem + ".expected-cascade.json";
                if (file_exists(expected_cascade_path)) {
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
                    std::string actual_str = out.done();
                    std::string expected_str = read_file(expected_cascade_path);
                    while (!expected_str.empty() && (expected_str.back() == '\n' || expected_str.back() == '\r'))
                        expected_str.pop_back();
                    cascade_s = (actual_str == expected_str) ? "PASS" : "FAIL";
                    if (cascade_s == "FAIL")
                        critical_total++;
                }
                std::string expected_layout_path = stem + ".expected-layout.json";
                if (file_exists(expected_layout_path)) {
                    browser::css::LayoutEngine layout_engine;
                    if (fs.ok) {
                        layout_engine.set_text_measure(fs.tr.get(), text_measure_cb);
                        layout_engine.set_text_metrics(fs.tr.get(), text_metrics_cb);
                    } else {
                        layout_engine.set_text_measure(nullptr, headless_text_measure);
                        layout_engine.set_text_metrics(nullptr, headless_text_metrics);
                    }
                    auto layout_r = layout_engine.layout_async(doc.get(), styles, 800.0f, 600.0f).sync_wait();
                    if (layout_r.is_ok()) {
                        auto layout = std::move(layout_r.unwrap());
                        std::string actual_str = dump_layout_node(layout.get());
                        std::string expected_str = read_file(expected_layout_path);
                        while (!expected_str.empty() && (expected_str.back() == '\n' || expected_str.back() == '\r'))
                            expected_str.pop_back();
                        layout_s = (actual_str == expected_str) ? "PASS" : "FAIL";
                        if (layout_s == "FAIL")
                            critical_total++;
                        std::string expected_disp_path = stem + ".expected-display-list.json";
                        if (file_exists(expected_disp_path)) {
                            browser::render::Painter painter(nullptr);
                            auto dl_r = painter.paint_async(layout.get()).sync_wait();
                            if (dl_r.is_ok()) {
                                auto dl = std::move(dl_r.unwrap());
                                json::Arr cmds;
                                for (auto &cmd : dl->commands()) cmds.push(dump_command(cmd));
                                std::string actual_str2 = cmds.done();
                                std::string expected_str2 = read_file(expected_disp_path);
                                while (!expected_str2.empty() &&
                                       (expected_str2.back() == '\n' || expected_str2.back() == '\r'))
                                    expected_str2.pop_back();
                                disp_s = (actual_str2 == expected_str2) ? "PASS" : "FAIL";
                                if (disp_s == "FAIL")
                                    critical_total++;
                            } else {
                                disp_s = "ERR";
                                critical_total++;
                            }
                        }
                    } else {
                        layout_s = "ERR";
                        critical_total++;
                    }
                }
            } else {
                cascade_s = "ERR";
                critical_total++;
            }
        } else {
            cascade_s = "ERR";
            critical_total++;
        }

        bool all_pass = (dom_s != "FAIL" && dom_s != "ERR") && (cascade_s != "FAIL" && cascade_s != "ERR") &&
                        (layout_s != "FAIL" && layout_s != "ERR") && (disp_s != "FAIL" && disp_s != "ERR");
        if (all_pass)
            passed_total++;
        else
            failed_total++;
        total++;
        fprintf(stderr,
                "%-42s DOM=%-4s CASCADE=%-6s LAYOUT=%-6s DISP=%-6s → %s\n",
                base.c_str(),
                dom_s.c_str(),
                cascade_s.c_str(),
                layout_s.c_str(),
                disp_s.c_str(),
                all_pass ? "PASS" : "FAIL");
        // Throttle: yield CPU between tests to prevent system lockup
        Sleep(200);
    }

    for (auto &css_file : css_files) {
        auto base = strip_path(css_file);
        auto stem = css_file.substr(0, css_file.size() - 4);
        std::string expected_path = stem + ".expected-css.json";
        std::string css_s = "SKIP";
        if (file_exists(expected_path)) {
            std::string content = read_file(css_file);
            browser::css::CssParser parser(content);
            auto sheet = parser.parse();
            std::string actual_str = dump_stylesheet(sheet);
            std::string expected_str = read_file(expected_path);
            if (!expected_str.empty() && expected_str.back() == '\n')
                expected_str.pop_back();
            if (!expected_str.empty() && expected_str.back() == '\r')
                expected_str.pop_back();
            css_s = (actual_str == expected_str) ? "PASS" : "FAIL";
            if (css_s == "FAIL")
                critical_total++;
        }
        if (css_s == "FAIL") {
            failed_total++;
        } else {
            passed_total++;
        }
        total++;
        fprintf(stderr, "%-42s CSS=%s → %s\n", base.c_str(), css_s.c_str(), css_s == "FAIL" ? "FAIL" : "PASS");
        Sleep(100);
    }

    fprintf(stderr,
            "\nTotal: %d  Passed: %d  Failed: %d  Critical: %d\n",
            total,
            passed_total,
            failed_total,
            critical_total);
    return failed_total > 0 ? 1 : 0;
}
