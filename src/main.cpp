#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <unordered_set>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "../html/parser.hpp"
#include "../html/traversal.hpp"
#include "../css/parser.hpp"
#include "../css/cascade.hpp"
#include "../css/layout.hpp"
#include "../render/paint.hpp"
#include "../browser/browser_window.hpp"

// ---------------------------------------------------------------------------
// JSON helpers – minimal, inline, no external dependency
// ---------------------------------------------------------------------------
namespace json {

    std::string esc(const std::string &s) {
        std::string o;
        o.reserve(s.size() + 8);
        for (unsigned char c : s) {
            switch (c) {
                case '"':  o += "\\\""; break;
                case '\\': o += "\\\\"; break;
                case '\n': o += "\\n";  break;
                case '\r': o += "\\r";  break;
                case '\t': o += "\\t";  break;
                default:
                    if (c < 0x20) {
                        char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c);
                        o += buf;
                    } else {
                        o += static_cast<char>(c);
                    }
            }
        }
        return o;
    }

    std::string val(const std::string &s) { return "\"" + esc(s) + "\""; }

    std::string val(f32 n, int prec = 4) {
        if (std::isnan(n)) return "null";
        if (std::isinf(n)) return n > 0 ? "null" : "null";
        std::ostringstream os;
        os << std::fixed << std::setprecision(prec) << n;
        std::string s = os.str();
        auto dot = s.find('.');
        if (dot != std::string::npos) {
            auto last = s.find_last_not_of('0');
            if (last > dot) s = s.substr(0, last + 1);
            else if (last == dot) s = s.substr(0, dot);
        }
        return s;
    }

    std::string val(bool b) { return b ? "true" : "false"; }

    struct Obj {
        std::string data = "{";
        bool first = true;
        void kv(const std::string &k, const std::string &v) {
            if (!first) data += ",";
            first = false;
            data += val(k) + ":" + v;
        }
        std::string done() { data += "}"; return data; }
    };

    struct Arr {
        std::string data = "[";
        bool first = true;
        void push(const std::string &v) {
            if (!first) data += ",";
            first = false;
            data += v;
        }
        std::string done() { data += "]"; return data; }
    };

} // namespace json

// ---------------------------------------------------------------------------
// DOM dump
// ---------------------------------------------------------------------------
static std::string dump_doctype(const browser::html::DocumentType *dt) {
    json::Obj o;
    o.kv("type", "doctype");
    o.kv("name", dt->name);
    o.kv("public_id", dt->public_id);
    o.kv("system_id", dt->system_id);
    return o.done();
}

static std::string dump_node(const browser::html::Node *node) {
    if (!node) return "null";
    if (node->type == browser::html::NodeType::ELEMENT) {
        auto *el = static_cast<const browser::html::Element *>(node);
        json::Obj o;
        o.kv("type", "element");
        o.kv("tag", el->tag_name);
        json::Obj attrs;
        for (auto &[k, v] : el->attributes) {
            attrs.kv(k, json::val(v));
        }
        o.kv("attributes", attrs.done());
        json::Arr kids;
        for (auto &ch : node->children) {
            kids.push(dump_node(ch.get()));
        }
        o.kv("children", kids.done());
        return o.done();
    }
    if (node->type == browser::html::NodeType::TEXT) {
        auto *tx = static_cast<const browser::html::Text *>(node);
        json::Obj o;
        o.kv("type", "text");
        o.kv("data", json::val(tx->data));
        std::string normalized = tx->data;
        {
            std::string r;
            bool last_was_space = false;
            for (char c : normalized) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if (!last_was_space) { r += ' '; last_was_space = true; }
                } else {
                    r += c; last_was_space = false;
                }
            }
            while (!r.empty() && r.back() == ' ') r.pop_back();
            normalized = r;
        }
        o.kv("data_normalized", json::val(normalized));
        return o.done();
    }
    if (node->type == browser::html::NodeType::COMMENT) {
        auto *cm = static_cast<const browser::html::Comment *>(node);
        json::Obj o;
        o.kv("type", "comment");
        o.kv("data", json::val(cm->data));
        return o.done();
    }
    if (node->type == browser::html::NodeType::DOCUMENT_TYPE) {
        return dump_doctype(static_cast<const browser::html::DocumentType *>(node));
    }
    return "null";
}

// ---------------------------------------------------------------------------
// CSS dump
// ---------------------------------------------------------------------------
static std::string css_val_type_str(browser::css::CSSValue::Type t) {
    switch (t) {
        case browser::css::CSSValue::Type::KEYWORD:    return "KEYWORD";
        case browser::css::CSSValue::Type::LENGTH:     return "LENGTH";
        case browser::css::CSSValue::Type::COLOR:      return "COLOR";
        case browser::css::CSSValue::Type::STRING:     return "STRING";
        case browser::css::CSSValue::Type::NUMBER:     return "NUMBER";
        case browser::css::CSSValue::Type::PERCENTAGE: return "PERCENTAGE";
        case browser::css::CSSValue::Type::URL:        return "URL";
        case browser::css::CSSValue::Type::FUNCTION:   return "FUNCTION";
        case browser::css::CSSValue::Type::GRADIENT:   return "GRADIENT";
        case browser::css::CSSValue::Type::TRANSFORM:  return "TRANSFORM";
    }
    return "UNKNOWN";
}

static std::string dump_declaration(const browser::css::Declaration &decl) {
    json::Obj o;
    o.kv("property", json::val(decl.property));
    {
        json::Arr vals;
        for (auto &v : decl.values) {
            json::Obj vobj;
            vobj.kv("type", json::val(css_val_type_str(v.type)));
            if (v.type == browser::css::CSSValue::Type::KEYWORD)
                vobj.kv("keyword", json::val(v.keyword));
            else if (v.type == browser::css::CSSValue::Type::LENGTH) {
                vobj.kv("value", json::val(v.length.value));
                std::string u;
                switch (v.length.unit) {
                    case browser::css::Length::Unit::PX:      u = "px"; break;
                    case browser::css::Length::Unit::EM:      u = "em"; break;
                    case browser::css::Length::Unit::REM:     u = "rem"; break;
                    case browser::css::Length::Unit::PERCENT: u = "%"; break;
                    case browser::css::Length::Unit::VW:      u = "vw"; break;
                    case browser::css::Length::Unit::VH:      u = "vh"; break;
                    default: u = "px"; break;
                }
                vobj.kv("unit", json::val(u));
            } else if (v.type == browser::css::CSSValue::Type::COLOR) {
                vobj.kv("r", json::val(static_cast<f32>(v.color.r)));
                vobj.kv("g", json::val(static_cast<f32>(v.color.g)));
                vobj.kv("b", json::val(static_cast<f32>(v.color.b)));
                vobj.kv("a", json::val(static_cast<f32>(v.color.a)));
            } else if (v.type == browser::css::CSSValue::Type::NUMBER ||
                       v.type == browser::css::CSSValue::Type::PERCENTAGE) {
                vobj.kv("number", json::val(v.number));
            } else if (v.type == browser::css::CSSValue::Type::STRING) {
                vobj.kv("string_value", json::val(v.string_value));
            } else if (v.type == browser::css::CSSValue::Type::FUNCTION) {
                vobj.kv("string_value", json::val(v.string_value));
            }
            vals.push(vobj.done());
        }
        o.kv("values", vals.done());
    }
    o.kv("important", json::val(decl.important));
    return o.done();
}

static std::string dump_rule(const browser::css::Rule &rule, int idx) {
    json::Obj o;
    o.kv("type", "rule");
    {
        json::Arr sels;
        for (auto &sel : rule.selectors) {
            std::string sel_str;
            for (size_t i = 0; i < sel.compounds.size(); i++) {
                if (i > 0) {
                    if (i - 1 < sel.combinators.size()) {
                        switch (sel.combinators[i - 1]) {
                            case browser::css::Combinator::DESCENDANT:        sel_str += " "; break;
                            case browser::css::Combinator::CHILD:             sel_str += " > "; break;
                            case browser::css::Combinator::ADJACENT_SIBLING:  sel_str += " + "; break;
                            case browser::css::Combinator::GENERAL_SIBLING:   sel_str += " ~ "; break;
                        }
                    } else sel_str += " ";
                }
                for (auto &ss : sel.compounds[i].simples) {
                    switch (ss.type) {
                        case browser::css::SimpleSelector::Type::TAG: sel_str += ss.name; break;
                        case browser::css::SimpleSelector::Type::CLASS: sel_str += "." + ss.name; break;
                        case browser::css::SimpleSelector::Type::ID: sel_str += "#" + ss.name; break;
                        case browser::css::SimpleSelector::Type::UNIVERSAL: sel_str += "*"; break;
                        case browser::css::SimpleSelector::Type::ATTRIBUTE: {
                            sel_str += "[" + ss.name;
                            if (ss.match_operator) {
                                sel_str += ss.match_operator;
                                sel_str += "=" + ss.value;
                            }
                            sel_str += "]";
                            break;
                        }
                        case browser::css::SimpleSelector::Type::PSEUDO_CLASS: {
                            if (ss.name == "nth-child" || ss.name == "nth-last-child") {
                                sel_str += ":" + ss.name + "(";
                                if (ss.nth_args.is_odd) sel_str += "odd";
                                else if (ss.nth_args.is_even) sel_str += "even";
                                else if (ss.nth_args.a == 0) sel_str += std::to_string(ss.nth_args.b);
                                else {
                                    sel_str += std::to_string(ss.nth_args.a) + "n";
                                    if (ss.nth_args.b > 0) sel_str += "+" + std::to_string(ss.nth_args.b);
                                    else if (ss.nth_args.b < 0) sel_str += std::to_string(ss.nth_args.b);
                                }
                                sel_str += ")";
                            } else {
                                sel_str += ":" + ss.name;
                                if (!ss.argument_selectors.empty()) {
                                    sel_str += "(";
                                    for (size_t ai = 0; ai < ss.argument_selectors.size(); ai++) {
                                        if (ai > 0) sel_str += ",";
                                        for (auto &cc : ss.argument_selectors[ai].compounds) {
                                            for (auto &sss : cc.simples) {
                                                if (sss.type == browser::css::SimpleSelector::Type::TAG) sel_str += sss.name;
                                                else if (sss.type == browser::css::SimpleSelector::Type::CLASS) sel_str += "." + sss.name;
                                                else if (sss.type == browser::css::SimpleSelector::Type::ID) sel_str += "#" + sss.name;
                                                else if (sss.type == browser::css::SimpleSelector::Type::UNIVERSAL) sel_str += "*";
                                            }
                                        }
                                    }
                                    sel_str += ")";
                                }
                            }
                            break;
                        }
                        case browser::css::SimpleSelector::Type::PSEUDO_ELEMENT: {
                            sel_str += "::" + ss.name;
                            break;
                        }
                    }
                }
            }
            sels.push(json::val(sel_str));
        }
        o.kv("selectors", sels.done());
    }
    {
        json::Arr decls;
        for (auto &decl : rule.declarations) {
            decls.push(dump_declaration(decl));
        }
        o.kv("declarations", decls.done());
    }
    o.kv("source_index", std::to_string(idx));
    return o.done();
}

static std::string dump_stylesheet(const browser::css::StyleSheet &sheet) {
    json::Arr rules;
    for (size_t i = 0; i < sheet.rules.size(); i++) {
        rules.push(dump_rule(sheet.rules[i], static_cast<int>(i)));
    }
    json::Arr atrules;
    for (auto &at : sheet.at_rules) {
        json::Obj a;
        a.kv("name", json::val(at.name));
        a.kv("prelude", json::val(at.prelude));
        json::Arr nested;
        for (auto &r : at.rules) nested.push(dump_rule(r, 0));
        a.kv("rules", nested.done());
        json::Arr nested_at;
        for (auto &na : at.at_rules) {
            json::Obj nao;
            nao.kv("name", json::val(na.name));
            nao.kv("prelude", json::val(na.prelude));
            nao.kv("rules", json::Arr().done());
            nao.kv("at_rules", json::Arr().done());
            nested_at.push(nao.done());
        }
        a.kv("at_rules", nested_at.done());
        // keyframes
        if (at.name == "keyframes" || at.name == "-webkit-keyframes") {
            json::Obj kf;
            kf.kv("name", json::val(at.keyframes.name));
            json::Arr blks;
            for (auto &blk : at.keyframes.blocks) {
                json::Obj b;
                json::Arr pos;
                for (auto p : blk.positions) pos.push(json::val(p));
                b.kv("positions", pos.done());
                json::Arr dcl;
                for (auto &d : blk.declarations) dcl.push(dump_declaration(d));
                b.kv("declarations", dcl.done());
                blks.push(b.done());
            }
            kf.kv("blocks", blks.done());
            a.kv("keyframes", kf.done());
        }
        atrules.push(a.done());
    }
    json::Obj out;
    out.kv("rules", rules.done());
    out.kv("at_rules", atrules.done());
    return out.done();
}

// ---------------------------------------------------------------------------
// Cascade dump
// ---------------------------------------------------------------------------
static std::string css_value_to_json(const browser::css::CSSValue &v) {
    json::Obj o;
    o.kv("type", json::val(css_val_type_str(v.type)));
    switch (v.type) {
        case browser::css::CSSValue::Type::KEYWORD:
            o.kv("value", json::val(v.keyword));
            break;
        case browser::css::CSSValue::Type::LENGTH:
            o.kv("value", json::val(v.length.value));
            {
                std::string u;
                switch (v.length.unit) {
                    case browser::css::Length::Unit::PX:      u = "px"; break;
                    case browser::css::Length::Unit::EM:      u = "em"; break;
                    case browser::css::Length::Unit::REM:     u = "rem"; break;
                    case browser::css::Length::Unit::PERCENT: u = "%"; break;
                    case browser::css::Length::Unit::VW:      u = "vw"; break;
                    case browser::css::Length::Unit::VH:      u = "vh"; break;
                    case browser::css::Length::Unit::NONE:    u = ""; break;
                    default: u = "px"; break;
                }
                o.kv("unit", json::val(u));
            }
            break;
        case browser::css::CSSValue::Type::COLOR:
            o.kv("r", json::val(static_cast<f32>(v.color.r)));
            o.kv("g", json::val(static_cast<f32>(v.color.g)));
            o.kv("b", json::val(static_cast<f32>(v.color.b)));
            o.kv("a", json::val(static_cast<f32>(v.color.a)));
            break;
        case browser::css::CSSValue::Type::NUMBER:
        case browser::css::CSSValue::Type::PERCENTAGE:
            o.kv("value", json::val(v.number));
            break;
        case browser::css::CSSValue::Type::STRING:
            o.kv("value", json::val(v.string_value));
            break;
        case browser::css::CSSValue::Type::URL:
            o.kv("value", json::val(v.string_value));
            break;
        default:
            o.kv("value", json::val(v.keyword));
            break;
    }
    return o.done();
}

static std::string dump_cascade_element(const browser::html::Element *el,
                                         const browser::css::ComputedStyle &style) {
    json::Obj o;
    o.kv("tag", json::val(el->tag_name));
    o.kv("id", json::val(el->id()));
    {
        json::Arr cls;
        for (auto &c : el->class_list()) cls.push(json::val(c));
        o.kv("classes", cls.done());
    }
    {
        json::Obj props;
        for (auto &[prop, val] : style.properties) {
            if (prop.size() > 1 && prop[0] == '_') continue;
            props.kv(prop, css_value_to_json(val));
        }
        o.kv("computed", props.done());
    }
    return o.done();
}

// ---------------------------------------------------------------------------
// Layout dump
// ---------------------------------------------------------------------------
static std::string dump_layout_node(const browser::css::LayoutNode *node) {
    if (!node) return "null";
    json::Obj o;
    if (!node->is_text()) {
        auto *n = node->node();
        if (n && n->type == browser::html::NodeType::ELEMENT) {
            o.kv("tag", json::val(static_cast<browser::html::Element*>(n)->tag_name));
        } else {
            o.kv("tag", json::val("(anonymous)"));
        }
    } else {
        o.kv("tag", json::val("(text)"));
    }
    o.kv("is_text", json::val(node->is_text()));
    o.kv("text", json::val(node->text()));
    {
        json::Obj r;
        r.kv("x", json::val(node->content.x));
        r.kv("y", json::val(node->content.y));
        r.kv("width", json::val(node->content.width));
        r.kv("height", json::val(node->content.height));
        o.kv("content", r.done());
    }
    {
        json::Obj r;
        r.kv("top", json::val(node->margin.top));
        r.kv("right", json::val(node->margin.right));
        r.kv("bottom", json::val(node->margin.bottom));
        r.kv("left", json::val(node->margin.left));
        o.kv("margin", r.done());
    }
    {
        json::Obj r;
        r.kv("top", json::val(node->padding.top));
        r.kv("right", json::val(node->padding.right));
        r.kv("bottom", json::val(node->padding.bottom));
        r.kv("left", json::val(node->padding.left));
        o.kv("padding", r.done());
    }
    {
        json::Obj r;
        r.kv("top", json::val(node->border.top));
        r.kv("right", json::val(node->border.right));
        r.kv("bottom", json::val(node->border.bottom));
        r.kv("left", json::val(node->border.left));
        o.kv("border", r.done());
    }
    if (!node->text_lines.empty()) {
        json::Arr lines;
        for (auto &li : node->text_lines) {
            json::Obj l;
            l.kv("y", json::val(li.y));
            l.kv("text", json::val(li.text));
            lines.push(l.done());
        }
        o.kv("text_lines", lines.done());
    }
    {
        json::Arr kids;
        for (auto &ch : node->children) {
            kids.push(dump_layout_node(ch.get()));
        }
        o.kv("children", kids.done());
    }
    return o.done();
}

// ---------------------------------------------------------------------------
// Display list dump
// ---------------------------------------------------------------------------
static std::string paint_cmd_type_str(browser::render::PaintCommand::Type t) {
    switch (t) {
        case browser::render::PaintCommand::Type::FILL_RECT:       return "FILL_RECT";
        case browser::render::PaintCommand::Type::DRAW_TEXT:       return "DRAW_TEXT";
        case browser::render::PaintCommand::Type::PUSH_CLIP:       return "PUSH_CLIP";
        case browser::render::PaintCommand::Type::POP_CLIP:        return "POP_CLIP";
        case browser::render::PaintCommand::Type::DRAW_IMAGE:      return "DRAW_IMAGE";
        case browser::render::PaintCommand::Type::DRAW_GRADIENT:   return "DRAW_GRADIENT";
        case browser::render::PaintCommand::Type::DRAW_SHADOW:     return "DRAW_SHADOW";
        case browser::render::PaintCommand::Type::PUSH_TRANSFORM:  return "PUSH_TRANSFORM";
        case browser::render::PaintCommand::Type::POP_TRANSFORM:   return "POP_TRANSFORM";
        case browser::render::PaintCommand::Type::PUSH_OPACITY:    return "PUSH_OPACITY";
        case browser::render::PaintCommand::Type::POP_OPACITY:     return "POP_OPACITY";
        case browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT: return "DRAW_ROUNDED_RECT";
        case browser::render::PaintCommand::Type::DRAW_CANVAS:     return "DRAW_CANVAS";
    }
    return "UNKNOWN";
}

static std::string render_color_to_hex(const browser::render::Color &c) {
    char buf[16];
    snprintf(buf, sizeof buf, "#%02x%02x%02x",
             static_cast<int>(c.r * 255),
             static_cast<int>(c.g * 255),
             static_cast<int>(c.b * 255));
    return buf;
}

static std::string dump_command(const browser::render::PaintCommand &cmd) {
    json::Obj o;
    o.kv("cmd", json::val(paint_cmd_type_str(cmd.type)));
    o.kv("x", json::val(cmd.rect.x));
    o.kv("y", json::val(cmd.rect.y));
    o.kv("w", json::val(cmd.rect.width));
    o.kv("h", json::val(cmd.rect.height));
    if (cmd.type == browser::render::PaintCommand::Type::FILL_RECT ||
        cmd.type == browser::render::PaintCommand::Type::DRAW_ROUNDED_RECT) {
        o.kv("color", json::val(render_color_to_hex(cmd.color)));
    }
    if (cmd.type == browser::render::PaintCommand::Type::DRAW_TEXT) {
        o.kv("text", json::val(cmd.text));
        o.kv("font_size", json::val(cmd.font_size));
        o.kv("color", json::val(render_color_to_hex(cmd.color)));
    }
    if (cmd.type == browser::render::PaintCommand::Type::DRAW_SHADOW) {
        o.kv("color", json::val(render_color_to_hex(cmd.color)));
    }
    if (cmd.radius > 0) {
        o.kv("radius", json::val(cmd.radius));
    }
    return o.done();
}

// ---------------------------------------------------------------------------
// Text measurer stub for headless layout
// ---------------------------------------------------------------------------
static f32 headless_text_measure(void *, const std::string &text, u32 pixel_size) {
    return static_cast<f32>(text.size()) * static_cast<f32>(pixel_size) * 0.6f;
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
static int run_browser(const std::string &url) {
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

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  browser --dump-dom <file.html>\n"
                  << "  browser --dump-css <file.css>\n"
                  << "  browser --dump-cascade <file.html>\n"
                  << "  browser --dump-layout <file.html>\n"
                  << "  browser --dump-display-list <file.html>\n"
                  << "  browser <url>\n";
        return 1;
    }

    std::string flag = argv[1];

    // Normal browser mode
    if (flag.rfind("--", 0) != 0) {
        SetProcessDPIAware();
        return run_browser(flag);
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
        auto doc = doc_r.unwrap();
        json::Obj out;
        out.kv("source", json::val(filepath));
        out.kv("encoding", json::val("utf-8"));
        json::Arr kids;
        for (auto &ch : doc->children) {
            kids.push(dump_node(ch.get()));
        }
        out.kv("children", kids.done());
        std::cout << out.done() << std::endl;
        return 0;
    }

    // --dump-css
    if (flag == "--dump-css") {
        browser::css::CssParser parser(content);
        auto sheet = parser.parse();
        std::cout << dump_stylesheet(sheet) << std::endl;
        return 0;
    }

    // --dump-cascade, --dump-layout, --dump-display-list
    if (flag == "--dump-cascade" || flag == "--dump-layout" || flag == "--dump-display-list") {
        SetProcessDPIAware();
        auto doc_r = browser::html::parse_async(content).sync_wait();
        if (doc_r.is_err()) {
            std::cerr << "HTML parse error: " << doc_r.unwrap_err() << std::endl;
            return 1;
        }
        auto doc = doc_r.unwrap();

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

        // --dump-cascade
        if (flag == "--dump-cascade") {
            json::Arr elements;
            browser::html::traverse_depth_first(doc.get(), [&](browser::html::Node *n) {
                if (n->type != browser::html::NodeType::ELEMENT) return;
                auto *el = static_cast<browser::html::Element *>(n);
                auto it = styles.find(el);
                if (it != styles.end()) {
                    elements.push(dump_cascade_element(el, it->second));
                }
            });
            json::Obj out;
            out.kv("elements", elements.done());
            std::cout << out.done() << std::endl;
            return 0;
        }

        // Layout
        browser::css::LayoutEngine layout_engine;
        layout_engine.set_text_measure(nullptr, headless_text_measure);
        auto layout_r = layout_engine.layout_async(doc.get(), styles, 800.0f, 600.0f).sync_wait();
        if (layout_r.is_err()) {
            std::cerr << "Layout error: " << layout_r.unwrap_err() << std::endl;
            return 1;
        }
        auto layout = layout_r.unwrap();

        // --dump-layout
        if (flag == "--dump-layout") {
            std::cout << dump_layout_node(layout.get()) << std::endl;
            return 0;
        }

        // --dump-display-list
        browser::render::Painter painter(nullptr);
        auto dl_r = painter.paint_async(layout.get()).sync_wait();
        if (dl_r.is_err()) {
            std::cerr << "Paint error: " << dl_r.unwrap_err() << std::endl;
            return 1;
        }
        auto dl = dl_r.unwrap();

        json::Arr cmds;
        for (auto &cmd : dl->commands()) {
            cmds.push(dump_command(cmd));
        }
        std::cout << cmds.done() << std::endl;
        return 0;
    }

    std::cerr << "Unknown flag: " << flag << std::endl;
    return 1;
}
