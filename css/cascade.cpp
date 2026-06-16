#include "cascade.hpp"
#include "parser.hpp"
#include "selector_match.hpp"
#include "../html/traversal.hpp"
#include "../async/executor.hpp"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <unordered_set>
#include <regex>

namespace browser::css {

static constexpr const char* UA_STYLESHEET = R"(
body { display: block; margin: 8px; }
div, p, h1, h2, h3, h4, h5, h6, ul, ol, li { display: block; }
table { display: table; }
tr, thead, tbody, tfoot { display: table-row; }
th, td { display: table-cell; }
caption { display: table-caption; }
pre, blockquote, article, aside, section, header, footer, nav, main, dl, dt, dd, details, summary, figure, figcaption, hr, form, fieldset, address, thead, tbody, tfoot, optgroup, option, select, button, textarea, input { display: block; }
b, i, u, s, span, a, strong, em, code, mark, sub, sup, small, label, abbr, cite, dfn, kbd, q, samp, tt, var { display: inline; }
h1 { font-size: 2em; font-weight: bold; }
h2 { font-size: 1.5em; font-weight: bold; }
h3 { font-size: 1.17em; font-weight: bold; }
h4 { font-size: 1em; font-weight: bold; }
h5 { font-size: 0.83em; font-weight: bold; }
h6 { font-size: 0.67em; font-weight: bold; }
p { margin-top: 1em; margin-bottom: 1em; }
ul, ol { padding-left: 40px; }
a { color: blue; }
strong { font-weight: bold; }
em { font-style: italic; }
code { font-family: monospace; }
)";

static constexpr const char* UA_STYLESHEET_PSEUDO = R"(
::before { display: inline; content: ''; }
::after { display: inline; content: ''; }
)";

bool is_inherited(const std::string& property) {
    return property == "color" ||
           property == "font-size" ||
           property == "font-family" ||
           property == "font-weight" ||
           property == "font-style" ||
           property == "line-height" ||
           property == "visibility" ||
           property == "cursor" ||
           property == "text-align" ||
           property == "white-space" ||
           property == "word-break" ||
           property == "overflow-wrap";
}

static bool evaluate_media_query(const std::string& prelude, f32 viewport_width, f32 viewport_height,
                                  f32 device_pixel_ratio, const std::string& color_scheme) {
    // Simplistic media query evaluation
    std::string lower;
    for (char c : prelude) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Default: always apply if no conditions
    if (lower.empty() || lower == "all") return true;

    // Check for not
    bool negate = false;
    if (lower.substr(0, 4) == "not ") {
        negate = true;
        lower = lower.substr(4);
    }

    // Check for "only" keyword
    if (lower.substr(0, 5) == "only ") {
        lower = lower.substr(5);
    }

    // Evaluate "and" separated conditions
    auto eval_single = [&](const std::string& cond) -> bool {
        if (cond == "all") return true;
        if (cond == "screen") return true;
        if (cond == "print") return false;

        // min-width: Npx
        std::regex minw_re(R"(min-width:\s*(\d+)\s*px)");
        std::smatch m;
        if (std::regex_search(cond, m, minw_re)) {
            f32 minw = std::stof(m[1].str());
            return viewport_width >= minw;
        }

        // max-width: Npx
        std::regex maxw_re(R"(max-width:\s*(\d+)\s*px)");
        if (std::regex_search(cond, m, maxw_re)) {
            f32 maxw = std::stof(m[1].str());
            return viewport_width <= maxw;
        }

        // min-height: Npx
        std::regex minh_re(R"(min-height:\s*(\d+)\s*px)");
        if (std::regex_search(cond, m, minh_re)) {
            f32 minh = std::stof(m[1].str());
            return viewport_height >= minh;
        }

        // max-height: Npx
        std::regex maxh_re(R"(max-height:\s*(\d+)\s*px)");
        if (std::regex_search(cond, m, maxh_re)) {
            f32 maxh = std::stof(m[1].str());
            return viewport_height <= maxh;
        }

        // width: Npx
        std::regex w_re(R"(width:\s*(\d+)\s*px)");
        if (std::regex_search(cond, m, w_re)) {
            f32 w = std::stof(m[1].str());
            return std::abs(viewport_width - w) < 0.5f;
        }

        // height: Npx
        std::regex h_re(R"(height:\s*(\d+)\s*px)");
        if (std::regex_search(cond, m, h_re)) {
            f32 h = std::stof(m[1].str());
            return std::abs(viewport_height - h) < 0.5f;
        }

        // prefers-color-scheme
        if (cond.find("prefers-color-scheme") != std::string::npos) {
            if (cond.find("dark") != std::string::npos) return color_scheme == "dark";
            if (cond.find("light") != std::string::npos) return color_scheme == "light";
        }

        // prefers-reduced-motion
        if (cond.find("prefers-reduced-motion") != std::string::npos) {
            if (cond.find("reduce") != std::string::npos) return false; // No reduced motion by default
            if (cond.find("no-preference") != std::string::npos) return true;
        }

        // orientation
        if (cond.find("orientation") != std::string::npos) {
            if (cond.find("portrait") != std::string::npos) return viewport_height > viewport_width;
            if (cond.find("landscape") != std::string::npos) return viewport_width >= viewport_height;
        }

        // resolution
        std::regex res_re(R"(resolution:\s*(\d+(?:\.\d+)?)\s*dpi)");
        if (std::regex_search(cond, m, res_re)) {
            f32 res = std::stof(m[1].str());
            return device_pixel_ratio * 96.0f >= res;
        }

        // hover
        if (cond.find("hover") != std::string::npos) {
            return true; // Assume hover-capable
        }

        // pointer
        if (cond.find("pointer") != std::string::npos) {
            if (cond.find("fine") != std::string::npos) return true;
            if (cond.find("coarse") != std::string::npos) return false;
        }

        // Unknown condition - default to true
        return true;
    };

    // Split by "and"
    bool result = true;
    size_t pos = 0;
    while (pos < lower.size()) {
        while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '(' || lower[pos] == ')')) pos++;
        if (pos >= lower.size()) break;
        size_t and_pos = lower.find(" and ", pos);
        std::string cond;
        if (and_pos == std::string::npos) {
            cond = lower.substr(pos);
            pos = lower.size();
        } else {
            cond = lower.substr(pos, and_pos - pos);
            pos = and_pos + 5;
        }
        // Trim whitespace and parentheses
        while (!cond.empty() && (cond.back() == ' ' || cond.back() == ')')) cond.pop_back();
        while (!cond.empty() && (cond[0] == ' ' || cond[0] == '(')) cond = cond.substr(1);

        if (!cond.empty() && cond != "and") {
            result = result && eval_single(cond);
        }
    }

    return negate ? !result : result;
}

// Extract pseudo-element name from the last compound of a selector
static std::string get_pseudo_element(const Selector& sel) {
    if (sel.compounds.empty()) return "";
    const auto& last = sel.compounds.back();
    for (const auto& ss : last.simples) {
        if (ss.type == SimpleSelector::Type::PSEUDO_ELEMENT) {
            return ss.name;
        }
    }
    return "";
}

static void collect_rules_from_sheet(const StyleSheet& sheet, const html::Element* el,
                                      const html::Document* doc, std::vector<MatchedDecl>& decls,
                                      u32& source_order, u8 origin,
                                      f32 viewport_width, f32 viewport_height,
                                      f32 device_pixel_ratio, const std::string& color_scheme) {
    for (const auto& rule : sheet.rules) {
        for (const auto& sel : rule.selectors) {
            if (matches_selector(sel, el, doc)) {
                std::string pe = get_pseudo_element(sel);
                for (const auto& decl : rule.declarations) {
                    decls.push_back({&decl, compute_specificity(sel), source_order++, origin, pe});
                }
                break;
            }
        }
    }
    for (const auto& at : sheet.at_rules) {
        std::string lower_name;
        for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_name == "media") {
            if (evaluate_media_query(at.prelude, viewport_width, viewport_height,
                                      device_pixel_ratio, color_scheme)) {
                for (const auto& rule : at.rules) {
                    for (const auto& sel : rule.selectors) {
                        if (matches_selector(sel, el, doc)) {
                            std::string pe = get_pseudo_element(sel);
                            for (const auto& decl : rule.declarations) {
                                decls.push_back({&decl, compute_specificity(sel), source_order++, origin, pe});
                            }
                            break;
                        }
                    }
                }
            }
        }
        for (const auto& nested : at.at_rules) {
            std::string nlower;
            for (char c : nested.name) nlower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (nlower == "media") {
                if (evaluate_media_query(nested.prelude, viewport_width, viewport_height,
                                          device_pixel_ratio, color_scheme)) {
                    for (const auto& rule : nested.rules) {
                        for (const auto& sel : rule.selectors) {
                            if (matches_selector(sel, el, doc)) {
                                std::string pe = get_pseudo_element(sel);
                                for (const auto& decl : rule.declarations) {
                                    decls.push_back({&decl, compute_specificity(sel), source_order++, origin, pe});
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

// Custom property resolution: resolve var(--name, fallback)
static std::string resolve_var(const std::string& value_str,
                                const ComputedStyle& style) {
    (void)style;
    std::string result = value_str;
    size_t var_pos = result.find("var(");
    int max_iterations = 64;
    while (var_pos != std::string::npos && --max_iterations >= 0) {
        size_t close_paren = var_pos + 4;
        int depth = 1;
        while (close_paren < result.size() && depth > 0) {
            if (result[close_paren] == '(') depth++;
            else if (result[close_paren] == ')') depth--;
            if (depth > 0) close_paren++;
        }
        if (depth != 0) break;

        std::string inner = result.substr(var_pos + 4, close_paren - var_pos - 4);
        while (!inner.empty() && inner[0] == ' ') inner = inner.substr(1);
        while (!inner.empty() && inner.back() == ' ') inner.pop_back();

        std::string var_name;
        std::string fallback;
        int cdepth = 0;
        size_t comma = std::string::npos;
        for (size_t i = 0; i < inner.size(); i++) {
            if (inner[i] == '(') cdepth++;
            else if (inner[i] == ')') cdepth--;
            else if (inner[i] == ',' && cdepth == 0) { comma = i; break; }
        }
        if (comma != std::string::npos) {
            var_name = inner.substr(0, comma);
            fallback = inner.substr(comma + 1);
            while (!fallback.empty() && fallback[0] == ' ') fallback = fallback.substr(1);
        } else {
            var_name = inner;
        }

        std::string replacement;
        if (var_name.size() >= 2 && var_name[0] == '-' && var_name[1] == '-') {
            auto it = style.properties.find(var_name);
            if (it != style.properties.end()) {
                const auto& v = it->second;
                replacement = v.string_value.empty() ? v.keyword : v.string_value;
            } else if (style.parent) {
                const ComputedStyle* parent = style.parent;
                while (parent) {
                    auto pit = parent->properties.find(var_name);
                    if (pit != parent->properties.end()) {
                        const auto& pv = pit->second;
                        replacement = pv.string_value.empty() ? pv.keyword : pv.string_value;
                        break;
                    }
                    parent = parent->parent;
                }
            }
        }

        if (replacement.empty()) replacement = fallback;

        result.replace(var_pos, close_paren - var_pos + 1, replacement);
        var_pos = result.find("var(", var_pos + replacement.size());
    }
    return result;
}

async::task<Cascade::CascadeResult>
Cascade::compute_async(const html::Document& doc, const StyleSheet& author,
                        f32 viewport_width, f32 viewport_height,
                        f32 device_pixel_ratio, const std::string& color_scheme) {
    co_await async::thread_pool_executor{};
    CssParser ua_parser(UA_STYLESHEET);
    StyleSheet ua = ua_parser.parse();
    CssParser ua_pseudo_parser(UA_STYLESHEET_PSEUDO);
    StyleSheet ua_pseudo = ua_pseudo_parser.parse();

    std::unordered_map<const html::Element*, std::vector<MatchedDecl>> matched;
    std::vector<std::shared_ptr<Declaration>> inline_decl_copies;
    u32 source_order = 0;

    // Phase 1: Collect
    auto* doc_node = const_cast<html::Document*>(&doc);
    html::traverse_depth_first(doc_node, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);

        std::vector<MatchedDecl> decls;

        // Collect from UA sheet
        collect_rules_from_sheet(ua, el, doc_node, decls, source_order, 0,
                                  viewport_width, viewport_height, device_pixel_ratio, color_scheme);

        // Collect from author sheet
        collect_rules_from_sheet(author, el, doc_node, decls, source_order, 1,
                                  viewport_width, viewport_height, device_pixel_ratio, color_scheme);

        // Collect inline style from element's style attribute (origin=2, highest)
        // NOTE: inline_decl_copies stores parsed declarations persistently so pointers remain valid
        std::string inline_style = el->get_attribute("style");
        if (!inline_style.empty()) {
            CssParser inline_parser(inline_style);
            StyleSheet inline_sheet = inline_parser.parse();
            for (const auto& rule : inline_sheet.rules) {
                for (const auto& decl : rule.declarations) {
                    Specificity spec;
                    spec.bits = 0;
                    inline_decl_copies.push_back(std::make_shared<Declaration>(decl));
                    decls.push_back({inline_decl_copies.back().get(), spec, source_order++, 2, ""});
                }
            }
        }

        matched[el] = std::move(decls);
    });

    // Phase 2: Sort
    for (auto& [el, decls] : matched) {
        std::sort(decls.begin(), decls.end(), [](const MatchedDecl& a, const MatchedDecl& b) {
            bool a_imp = a.decl->important;
            bool b_imp = b.decl->important;
            if (a_imp != b_imp) return a_imp > b_imp;
            if (!a_imp) {
                if (a.origin != b.origin) return a.origin < b.origin;
            } else {
                if (a.origin != b.origin) return a.origin > b.origin;
            }
            if (a.specificity.bits != b.specificity.bits) return a.specificity.bits < b.specificity.bits;
            return a.source_order < b.source_order;
        });
    }

    // Phase 3: Apply
    std::unordered_map<const html::Element*, ComputedStyle> styles;

    html::traverse_depth_first(doc_node, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);

        ComputedStyle style;
        auto& decls = matched[el];
        for (const auto& md : decls) {
            if (!md.decl->values.empty()) {
                std::string prop = md.decl->property;

                // Route pseudo-element properties to special storage
                if (!md.pseudo_element.empty()) {
                    // For pseudo-elements, store the property with a prefix to avoid conflicts
                    std::string pe_key = "_" + md.pseudo_element + "_" + prop;
                    if (md.decl->values.size() == 1 && md.decl->values[0].type == CSSValue::Type::STRING) {
                        // Handle var() resolution for pseudo-element content
                        CSSValue v = md.decl->values[0];
                        if (v.string_value.find("var(") != std::string::npos) {
                            v.string_value = resolve_var(v.string_value, style);
                        }
                        style.properties[pe_key] = v;
                    } else {
                        CSSValue combined;
                        combined.type = CSSValue::Type::STRING;
                        for (size_t vi = 0; vi < md.decl->values.size(); vi++) {
                            if (vi > 0) combined.string_value += ' ';
                            const auto& val = md.decl->values[vi];
                            if (val.type == CSSValue::Type::STRING) combined.string_value += val.string_value;
                            else if (val.type == CSSValue::Type::KEYWORD) combined.string_value += val.keyword;
                        }
                        style.properties[pe_key] = combined;
                    }
                    continue; // Skip normal property application for pseudo-elements
                }

                // Detect custom properties (--*)
                if (prop.size() >= 2 && prop[0] == '-' && prop[1] == '-') {
                    // Store as string value
                    CSSValue cv;
                    cv.type = CSSValue::Type::STRING;
                    for (size_t vi = 0; vi < md.decl->values.size(); vi++) {
                        if (vi > 0) cv.string_value += ' ';
                        const auto& val = md.decl->values[vi];
                        switch (val.type) {
                            case CSSValue::Type::KEYWORD: cv.string_value += val.keyword; break;
                            case CSSValue::Type::STRING: cv.string_value += val.string_value; break;
                            case CSSValue::Type::NUMBER: cv.string_value += std::to_string(val.number); break;
                            case CSSValue::Type::LENGTH: {
                                cv.string_value += std::to_string(val.length.value);
                                switch (val.length.unit) {
                                    case Length::Unit::PX: cv.string_value += "px"; break;
                                    case Length::Unit::EM: cv.string_value += "em"; break;
                                    case Length::Unit::REM: cv.string_value += "rem"; break;
                                    case Length::Unit::PERCENT: cv.string_value += "%"; break;
                                    case Length::Unit::VW: cv.string_value += "vw"; break;
                                    case Length::Unit::VH: cv.string_value += "vh"; break;
                                    default: break;
                                }
                                break;
                            }
                            case CSSValue::Type::COLOR: {
                                char buf[16];
                                snprintf(buf, sizeof(buf), "#%02x%02x%02x", val.color.r, val.color.g, val.color.b);
                                cv.string_value += buf;
                                break;
                            }
                            default: cv.string_value += val.keyword; break;
                        }
                    }
                    style.properties[prop] = cv;
                    continue;
                }

                if (md.decl->values.size() == 1) {
                    CSSValue val = md.decl->values[0];
                    // Resolve var() in string/function values
                        if (val.type == CSSValue::Type::KEYWORD && val.keyword.substr(0, 4) == "var(") {
                            std::string resolved = resolve_var(val.keyword, style);
                        CSSValue cv;
                        // Try to re-parse as number/length/color
                        if ((!resolved.empty() && std::isdigit(static_cast<unsigned char>(resolved[0]))) ||
                            (!resolved.empty() && (resolved[0] == '-' || resolved[0] == '+'))) {
                            char* end = nullptr;
                            f32 num = std::strtof(resolved.c_str(), &end);
                            if (end && end != resolved.c_str()) {
                                std::string unit = end;
                                if (unit == "px") { cv.type = CSSValue::Type::LENGTH; cv.length = {num, Length::Unit::PX}; }
                                else if (unit == "em") { cv.type = CSSValue::Type::LENGTH; cv.length = {num, Length::Unit::EM}; }
                                else if (unit == "%") { cv.type = CSSValue::Type::PERCENTAGE; cv.number = num; }
                                else if (unit.empty()) { cv.type = CSSValue::Type::NUMBER; cv.number = num; }
                                else { cv.type = CSSValue::Type::STRING; cv.string_value = resolved; }
                            } else {
                                cv.type = CSSValue::Type::STRING;
                                cv.string_value = resolved;
                            }
                        } else if (!resolved.empty() && resolved[0] == '#') {
                            cv.type = CSSValue::Type::COLOR;
                            cv.color = Color::from_hex(resolved);
                        } else {
                            auto named = Color::from_name(resolved);
                            if (named.a != 0 || resolved == "transparent") {
                                cv.type = CSSValue::Type::COLOR;
                                cv.color = named;
                            } else {
                                cv.type = CSSValue::Type::KEYWORD;
                                cv.keyword = resolved;
                            }
                        }
                        style.properties[prop] = cv;
                    } else if (val.type == CSSValue::Type::STRING && val.string_value.find("var(") != std::string::npos) {
                        std::string resolved = resolve_var(val.string_value, style);
                        val.string_value = resolved;
                        style.properties[prop] = val;
                    } else {
                        style.properties[prop] = val;
                    }
                } else {
                    CSSValue combined;
                    combined.type = CSSValue::Type::STRING;
                    for (std::size_t vi = 0; vi < md.decl->values.size(); vi++) {
                        if (vi > 0) combined.string_value += ' ';
                        const auto& val = md.decl->values[vi];
                        switch (val.type) {
                            case CSSValue::Type::KEYWORD:
                                combined.string_value += val.keyword;
                                break;
                            case CSSValue::Type::LENGTH: {
                                std::string num = std::to_string(val.length.value);
                                auto dot = num.find('.');
                                if (dot != std::string::npos) {
                                    auto last = num.find_last_not_of('0');
                                    if (last > dot) num = num.substr(0, last + 1);
                                    else if (last == dot) num = num.substr(0, dot);
                                }
                                combined.string_value += num;
                                switch (val.length.unit) {
                                    case Length::Unit::PX: combined.string_value += "px"; break;
                                    case Length::Unit::EM: combined.string_value += "em"; break;
                                    case Length::Unit::REM: combined.string_value += "rem"; break;
                                    case Length::Unit::PERCENT: combined.string_value += "%"; break;
                                    case Length::Unit::VW: combined.string_value += "vw"; break;
                                    case Length::Unit::VH: combined.string_value += "vh"; break;
                                    case Length::Unit::NONE: combined.string_value += "fr"; break;
                                    case Length::Unit::DEG: combined.string_value += "deg"; break;
                                    case Length::Unit::S: combined.string_value += "s"; break;
                                    case Length::Unit::MS: combined.string_value += "ms"; break;
                                }
                                break;
                            }
                            case CSSValue::Type::NUMBER:
                                combined.string_value += std::to_string(val.number);
                                break;
                            case CSSValue::Type::STRING:
                                combined.string_value += val.string_value;
                                break;
                            case CSSValue::Type::FUNCTION:
                                if (!val.string_value.empty()) {
                                    combined.string_value += val.string_value;
                                } else {
                                    combined.string_value += val.keyword + "(...)";
                                }
                                break;
                            case CSSValue::Type::COLOR:
                            case CSSValue::Type::PERCENTAGE:
                            case CSSValue::Type::URL:
                                break;
                            default:
                                break;
                        }
                    }
                    style.properties[prop] = std::move(combined);
                }

                // Expand shorthands
                {
                    const std::string& prop = md.decl->property;
                    CSSValue val = style.properties[md.decl->property];

                    if ((prop == "border" || prop == "border-top" || prop == "border-right" ||
                         prop == "border-bottom" || prop == "border-left") &&
                        md.decl->values.size() > 1) {
                        for (const auto& v : md.decl->values) {
                            if (v.type == CSSValue::Type::LENGTH) {
                                CSSValue bwidth = v;
                                if (prop == "border") {
                                    style.properties["border-top-width"] = bwidth;
                                    style.properties["border-right-width"] = bwidth;
                                    style.properties["border-bottom-width"] = bwidth;
                                    style.properties["border-left-width"] = bwidth;
                                } else {
                                    style.properties[prop + "-width"] = bwidth;
                                }
                            }
                            if (v.type == CSSValue::Type::COLOR) {
                                CSSValue bcolor = v;
                                if (prop == "border") {
                                    style.properties["border-top-color"] = bcolor;
                                    style.properties["border-right-color"] = bcolor;
                                    style.properties["border-bottom-color"] = bcolor;
                                    style.properties["border-left-color"] = bcolor;
                                } else {
                                    style.properties[prop + "-color"] = bcolor;
                                }
                            }
                        }
                    }

                    // margin shorthand expansion
                    auto expand_four_sides = [&](const std::string& base, const std::string& val_str) {
                        std::vector<std::string> parts;
                        std::string s = val_str;
                        size_t pp = 0;
                        while (pp < s.size()) {
                            while (pp < s.size() && s[pp] == ' ') pp++;
                            if (pp >= s.size()) break;
                            size_t end = s.find(' ', pp);
                            if (end == std::string::npos) end = s.size();
                            parts.push_back(s.substr(pp, end - pp));
                            pp = end + 1;
                        }
                        auto set_side_fn = [&](const std::string& side, const std::string& pv) {
                            if (pv.empty() || style.has(side)) return;
                            CSSValue cv;
                            if (pv == "auto") { cv.type = CSSValue::Type::KEYWORD; cv.keyword = "auto"; }
                            else {
                                cv.type = CSSValue::Type::STRING; cv.string_value = pv;
                                char* endp = nullptr;
                                f32 num = std::strtof(pv.c_str(), &endp);
                                if (endp != pv.c_str()) {
                                    cv.type = CSSValue::Type::LENGTH; cv.length.value = num;
                                    std::string unit = endp;
                                    if (unit == "px") cv.length.unit = Length::Unit::PX;
                                    else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                    else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                    else if (unit == "%") cv.length.unit = Length::Unit::PERCENT;
                                    else { cv.type = CSSValue::Type::STRING; cv.string_value = pv; }
                                }
                            }
                            style.properties[side] = cv;
                        };
                        if (parts.size() == 1) {
                            set_side_fn(base + "-top", parts[0]);
                            set_side_fn(base + "-right", parts[0]);
                            set_side_fn(base + "-bottom", parts[0]);
                            set_side_fn(base + "-left", parts[0]);
                        } else if (parts.size() == 2) {
                            set_side_fn(base + "-top", parts[0]);
                            set_side_fn(base + "-right", parts[1]);
                            set_side_fn(base + "-bottom", parts[0]);
                            set_side_fn(base + "-left", parts[1]);
                        } else if (parts.size() == 3) {
                            set_side_fn(base + "-top", parts[0]);
                            set_side_fn(base + "-right", parts[1]);
                            set_side_fn(base + "-bottom", parts[2]);
                            set_side_fn(base + "-left", parts[1]);
                        } else if (parts.size() == 4) {
                            set_side_fn(base + "-top", parts[0]);
                            set_side_fn(base + "-right", parts[1]);
                            set_side_fn(base + "-bottom", parts[2]);
                            set_side_fn(base + "-left", parts[3]);
                        }
                    };

                    if (prop == "margin" && val.type == CSSValue::Type::STRING) {
                        expand_four_sides("margin", val.string_value);
                    }
                    if (prop == "padding" && val.type == CSSValue::Type::STRING) {
                        expand_four_sides("padding", val.string_value);
                    }

                    auto expand_border_side = [&](const std::string& side, const CSSValue& bval) {
                        if (!style.has(side + "-width") && !style.has("border-width"))
                            style.properties[side + "-width"] = bval;
                    };
                    if (prop == "border" && val.type == CSSValue::Type::STRING) {
                        expand_border_side("border-top", val);
                        expand_border_side("border-right", val);
                        expand_border_side("border-bottom", val);
                        expand_border_side("border-left", val);
                    }

                    if (prop == "border-width" && val.type == CSSValue::Type::STRING) {
                        expand_four_sides("border", val.string_value + "-width");
                    }

                    // flex shorthand expansion
                    if (prop == "flex" && val.type == CSSValue::Type::STRING) {
                        std::string s = val.string_value;
                        // Handle "flex: none"
                        {
                            std::string trimmed = s;
                            while (!trimmed.empty() && trimmed[0] == ' ') trimmed = trimmed.substr(1);
                            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
                            if (trimmed == "none") {
                                auto set_flex_val = [&](const std::string& subprop, f32 num) {
                                    CSSValue cv; cv.type = CSSValue::Type::NUMBER; cv.number = num;
                                    style.properties[subprop] = cv;
                                };
                                set_flex_val("flex-grow", 0);
                                set_flex_val("flex-shrink", 0);
                                {
                                    CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = "auto";
                                    style.properties["flex-basis"] = cv;
                                }
                            } else {
                                std::vector<std::string> parts;
                                size_t pp = 0;
                                while (pp < s.size()) {
                                    while (pp < s.size() && s[pp] == ' ') pp++;
                                    if (pp >= s.size()) break;
                                    size_t end = s.find(' ', pp);
                                    if (end == std::string::npos) end = s.size();
                                    parts.push_back(s.substr(pp, end - pp));
                                    pp = end + 1;
                                }
                                auto set_flex_num = [&](const std::string& subprop, const std::string& pv) {
                                    CSSValue cv;
                                    char* endp = nullptr;
                                    f32 num = std::strtof(pv.c_str(), &endp);
                                    if (endp != pv.c_str()) {
                                        cv.type = CSSValue::Type::NUMBER;
                                        cv.number = num;
                                        style.properties[subprop] = cv;
                                    }
                                };
                                auto set_flex_basis = [&](const std::string& pv) {
                                    CSSValue cv;
                                    char* endp = nullptr;
                                    f32 num = std::strtof(pv.c_str(), &endp);
                                    if (endp != pv.c_str()) {
                                        cv.type = CSSValue::Type::LENGTH;
                                        cv.length.value = num;
                                        std::string unit = endp;
                                        if (unit == "px") cv.length.unit = Length::Unit::PX;
                                        else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                        else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                        else if (unit == "%") cv.length.unit = Length::Unit::PERCENT;
                                        else { cv.type = CSSValue::Type::KEYWORD; cv.keyword = pv; }
                                    } else {
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = pv;
                                    }
                                    style.properties["flex-basis"] = cv;
                                };
                                // flex: <grow> <shrink> <basis>?
                                // If 1 part: <grow> (grow=val, shrink=1, basis=0%)
                                // If 2 parts: <grow> <shrink>
                                // If 3 parts: <grow> <shrink> <basis>
                                if (parts.size() >= 1) set_flex_num("flex-grow", parts[0]);
                                if (parts.size() >= 2) set_flex_num("flex-shrink", parts[1]);
                                if (parts.size() >= 3) set_flex_basis(parts[2]);
                                if (parts.size() == 1) {
                                    // Just flex-grow set; default shrink=1, basis=0%
                                    {
                                        CSSValue cv; cv.type = CSSValue::Type::NUMBER; cv.number = 1;
                                        style.properties["flex-shrink"] = cv;
                                    }
                                    {
                                        CSSValue cv; cv.type = CSSValue::Type::LENGTH; cv.length = {0, Length::Unit::PX};
                                        style.properties["flex-basis"] = cv;
                                    }
                                }
                                if (parts.size() == 2) {
                                    CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = "auto";
                                    if (!style.has("flex-basis")) style.properties["flex-basis"] = cv;
                                }
                            }
                        }
                    }

                    if (prop == "background" && (val.type == CSSValue::Type::STRING || val.type == CSSValue::Type::COLOR)) {
                        if (!style.has("background-color"))
                            style.properties["background-color"] = val;
                    }

                    // animation shorthand
                    if (prop == "animation" && val.type == CSSValue::Type::STRING) {
                        std::string s = val.string_value;
                        size_t sp = 0;
                        std::vector<std::string> parts;
                        while (sp < s.size()) {
                            while (sp < s.size() && s[sp] == ' ') sp++;
                            if (sp >= s.size()) break;
                            size_t end = s.find(' ', sp);
                            if (end == std::string::npos) end = s.size();
                            parts.push_back(s.substr(sp, end - sp));
                            sp = end + 1;
                        }

                        auto set_anim = [&](const std::string& subprop, const std::string& v) {
                            CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = v;
                            style.properties[subprop] = cv;
                        };

                        // Very simplified animation shorthand parse
                        // First numeric value with 's' or 'ms' is duration
                        // Second numeric value is delay
                        // After that, keywords map to their respective sub-properties
                        int num_seen = 0;
                        for (const auto& p : parts) {
                            if (!p.empty() && (std::isdigit(static_cast<unsigned char>(p[0])) || p[0] == '.')) {
                                char* end = nullptr;
                                f32 num = std::strtof(p.c_str(), &end);
                                if (end && *end != '\0') {
                                    if (num_seen == 0) {
                                        if (std::string(end) == "ms") set_anim("animation-duration", std::to_string(num / 1000.0f) + "s");
                                        else set_anim("animation-duration", p);
                                        num_seen++;
                                    }
                                } else {
                                    num_seen++;
                                }
                            }
                        }

                        if (!parts.empty() && parts[0] != "infinite") {
                            std::string first = parts[0];
                            if (first != "ease" && first != "linear" && first != "ease-in" &&
                                first != "ease-out" && first != "ease-in-out" &&
                                first.substr(0, 6) != "cubic-" && first.substr(0, 6) != "steps(" &&
                                first.find("ms") == std::string::npos && first.find('s') == std::string::npos) {
                                set_anim("animation-name", first);
                            }
                        }
                    }
                }
            }
        }

        styles[el] = std::move(style);
    });

    // Relink parent pointers
    html::traverse_depth_first(doc_node, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);
        if (el->parent && el->parent->type == html::NodeType::ELEMENT) {
            auto* parent_el = static_cast<html::Element*>(el->parent);
            auto it = styles.find(parent_el);
            auto self = styles.find(el);
            if (it != styles.end() && self != styles.end()) {
                self->second.parent = &it->second;
            }
        }
    });

    co_return CascadeResult{std::move(styles)};
}

}
