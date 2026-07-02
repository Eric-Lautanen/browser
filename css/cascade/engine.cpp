#include "engine.hpp"

#include "../../async/executor.hpp"
#include "../../html/traversal.hpp"
#include "../parser.hpp"
#include "../selector_match.hpp"

#include <cstdlib>
#include <memory>

namespace browser::css {

    static constexpr const char *UA_STYLESHEET = R"(
body { display: block; margin: 8px; }
div, p, h1, h2, h3, h4, h5, h6, ul, ol { display: block; }
li { display: list-item; }
table { display: table; }
tr { display: table-row; }
thead { display: table-header-group; }
tbody { display: table-row-group; }
tfoot { display: table-footer-group; }
th, td { display: table-cell; }
caption { display: table-caption; }
pre, blockquote, article, aside, section, header, footer, nav, main, dl, dt, dd, details, summary, figure, figcaption, hr, form, fieldset, address, optgroup, option, select, button, textarea, input { display: block; }
head, link, meta, title, style, script, noscript { display: none; }
b, i, u, s, span, a, strong, em, code, mark, sub, sup, small, label, abbr, cite, dfn, kbd, q, samp, tt, var { display: inline; }
h1 { font-size: 2em; font-weight: bold; margin-top: 0.67em; margin-bottom: 0.67em; }
h2 { font-size: 1.5em; font-weight: bold; margin-top: 0.83em; margin-bottom: 0.83em; }
h3 { font-size: 1.17em; font-weight: bold; margin-top: 1em; margin-bottom: 1em; }
h4 { font-size: 1em; font-weight: bold; margin-top: 1.33em; margin-bottom: 1.33em; }
h5 { font-size: 0.83em; font-weight: bold; margin-top: 1.67em; margin-bottom: 1.67em; }
h6 { font-size: 0.67em; font-weight: bold; margin-top: 2.33em; margin-bottom: 2.33em; }
p { margin-top: 1em; margin-bottom: 1em; }
ul, ol { padding-left: 40px; }
a { color: blue; }
strong { font-weight: bold; }
em { font-style: italic; }
code { font-family: monospace; }
)";

    static constexpr const char *UA_STYLESHEET_PSEUDO = R"(
::before { display: inline; content: ''; }
::after { display: inline; content: ''; }
)";

    static std::string get_pseudo_element(const Selector &sel) {
        if (sel.compounds.empty())
            return "";
        const auto &last = sel.compounds.back();
        for (const auto &ss : last.simples) {
            if (ss.type == SimpleSelector::Type::PSEUDO_ELEMENT) {
                return ss.name;
            }
        }
        return "";
    }

    static std::string resolve_var(const std::string &value_str, const ComputedStyle &style) {
        (void)style;
        std::string result = value_str;
        size_t var_pos = result.find("var(");
        int max_iterations = 64;
        while (var_pos != std::string::npos && --max_iterations >= 0) {
            size_t close_paren = var_pos + 4;
            int depth = 1;
            while (close_paren < result.size() && depth > 0) {
                if (result[close_paren] == '(')
                    depth++;
                else if (result[close_paren] == ')')
                    depth--;
                if (depth > 0)
                    close_paren++;
            }
            if (depth != 0)
                break;

            std::string inner = result.substr(var_pos + 4, close_paren - var_pos - 4);
            while (!inner.empty() && inner[0] == ' ') inner = inner.substr(1);
            while (!inner.empty() && inner.back() == ' ') inner.pop_back();

            std::string var_name;
            std::string fallback;
            int cdepth = 0;
            size_t comma = std::string::npos;
            for (size_t i = 0; i < inner.size(); i++) {
                if (inner[i] == '(')
                    cdepth++;
                else if (inner[i] == ')')
                    cdepth--;
                else if (inner[i] == ',' && cdepth == 0) {
                    comma = i;
                    break;
                }
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
                    const auto &v = it->second;
                    replacement = v.string_value.empty() ? v.keyword : v.string_value;
                } else if (style.parent) {
                    const ComputedStyle *parent = style.parent;
                    while (parent) {
                        auto pit = parent->properties.find(var_name);
                        if (pit != parent->properties.end()) {
                            const auto &pv = pit->second;
                            replacement = pv.string_value.empty() ? pv.keyword : pv.string_value;
                            break;
                        }
                        parent = parent->parent;
                    }
                }
            }

            if (replacement.empty())
                replacement = fallback;

            result.replace(var_pos, close_paren - var_pos + 1, replacement);
            var_pos = result.find("var(", var_pos + replacement.size());
        }
        return result;
    }

    static void collect_rules_from_sheet(const StyleSheet &sheet,
                                         const html::Element *el,
                                         const html::Document *doc,
                                         std::vector<MatchedDecl> &decls,
                                         u32 &source_order,
                                         u8 origin,
                                         f32 viewport_width,
                                         f32 viewport_height,
                                         f32 device_pixel_ratio,
                                         const std::string &color_scheme) {
        for (const auto &rule : sheet.rules) {
            for (const auto &sel : rule.selectors) {
                if (matches_selector(sel, el, doc)) {
                    std::string pe = get_pseudo_element(sel);
                    for (const auto &decl : rule.declarations) {
                        decls.push_back({&decl, compute_specificity(sel), source_order++, origin, pe});
                    }
                    break;
                }
            }
        }
        for (const auto &at : sheet.at_rules) {
            std::string lower_name;
            for (char c : at.name) lower_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower_name == "media") {
                if (evaluate_media_query(
                        at.prelude, viewport_width, viewport_height, device_pixel_ratio, color_scheme)) {
                    for (const auto &rule : at.rules) {
                        for (const auto &sel : rule.selectors) {
                            if (matches_selector(sel, el, doc)) {
                                std::string pe = get_pseudo_element(sel);
                                for (const auto &decl : rule.declarations) {
                                    decls.push_back({&decl, compute_specificity(sel), source_order++, origin, pe});
                                }
                                break;
                            }
                        }
                    }
                }
            }
            for (const auto &nested : at.at_rules) {
                std::string nlower;
                for (char c : nested.name) nlower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nlower == "media") {
                    if (evaluate_media_query(
                            nested.prelude, viewport_width, viewport_height, device_pixel_ratio, color_scheme)) {
                        for (const auto &rule : nested.rules) {
                            for (const auto &sel : rule.selectors) {
                                if (matches_selector(sel, el, doc)) {
                                    std::string pe = get_pseudo_element(sel);
                                    for (const auto &decl : rule.declarations) {
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

    async::task<Cascade::CascadeResult> Cascade::compute_async(const html::Document &doc,
                                                               const StyleSheet &author,
                                                               f32 viewport_width,
                                                               f32 viewport_height,
                                                               f32 device_pixel_ratio,
                                                               const std::string &color_scheme) {
        co_await async::thread_pool_executor{};
        CssParser ua_parser(UA_STYLESHEET);
        StyleSheet ua = ua_parser.parse();
        CssParser ua_pseudo_parser(UA_STYLESHEET_PSEUDO);
        StyleSheet ua_pseudo = ua_pseudo_parser.parse();
        // Merge pseudo-element UA rules into the main UA sheet
        for (auto &rule : ua_pseudo.rules) {
            ua.rules.push_back(std::move(rule));
        }

        std::unordered_map<const html::Element *, std::vector<MatchedDecl>> matched;
        std::vector<std::shared_ptr<Declaration>> inline_decl_copies;
        u32 source_order = 0;

        auto *doc_node = const_cast<html::Document *>(&doc);
        html::traverse_depth_first(doc_node, [&](html::Node *node) {
            if (node->type != html::NodeType::ELEMENT)
                return;
            auto *el = static_cast<html::Element *>(node);

            std::vector<MatchedDecl> decls;

            collect_rules_from_sheet(ua,
                                     el,
                                     doc_node,
                                     decls,
                                     source_order,
                                     0,
                                     viewport_width,
                                     viewport_height,
                                     device_pixel_ratio,
                                     color_scheme);

            collect_rules_from_sheet(author,
                                     el,
                                     doc_node,
                                     decls,
                                     source_order,
                                     1,
                                     viewport_width,
                                     viewport_height,
                                     device_pixel_ratio,
                                     color_scheme);

            std::string inline_style = el->get_attribute("style");
            if (!inline_style.empty()) {
                CssParser inline_parser(inline_style);
                auto inline_decls = inline_parser.parse_inline_declarations();
                for (const auto &decl : inline_decls) {
                    Specificity spec;
                    spec.bits = 0;
                    inline_decl_copies.push_back(std::make_shared<Declaration>(decl));
                    decls.push_back({inline_decl_copies.back().get(), spec, source_order++, 2, ""});
                }
            }

            matched[el] = std::move(decls);
        });

        for (auto &[el, decls] : matched) {
            (void)el;
            sort_matched_decls(decls);
        }

        std::unordered_map<const html::Element *, ComputedStyle> styles;

        html::traverse_depth_first(doc_node, [&](html::Node *node) {
            if (node->type != html::NodeType::ELEMENT)
                return;
            auto *el = static_cast<html::Element *>(node);

            ComputedStyle style;
            style._element = el;

            auto &decls = matched[el];
            for (const auto &md : decls) {
                if (!md.decl->values.empty()) {
                    std::string prop = md.decl->property;

                    if (!md.pseudo_element.empty()) {
                        std::string pe_key = "_" + md.pseudo_element + "_" + prop;
                        if (md.decl->values.size() == 1 && md.decl->values[0].type == CSSValue::Type::STRING) {
                            CSSValue v = md.decl->values[0];
                            if (v.string_value.find("var(") != std::string::npos) {
                                v.string_value = resolve_var(v.string_value, style);
                            }
                            style.properties[pe_key] = v;
                        } else {
                            CSSValue combined;
                            combined.type = CSSValue::Type::STRING;
                            for (size_t vi = 0; vi < md.decl->values.size(); vi++) {
                                if (vi > 0)
                                    combined.string_value += ' ';
                                const auto &val = md.decl->values[vi];
                                if (val.type == CSSValue::Type::STRING)
                                    combined.string_value += val.string_value;
                                else if (val.type == CSSValue::Type::KEYWORD)
                                    combined.string_value += val.keyword;
                            }
                            style.properties[pe_key] = combined;
                        }
                        continue;
                    }

                    if (prop.size() >= 2 && prop[0] == '-' && prop[1] == '-') {
                        CSSValue cv;
                        cv.type = CSSValue::Type::STRING;
                        for (size_t vi = 0; vi < md.decl->values.size(); vi++) {
                            if (vi > 0)
                                cv.string_value += ' ';
                            const auto &val = md.decl->values[vi];
                            switch (val.type) {
                                case CSSValue::Type::KEYWORD:
                                    cv.string_value += val.keyword;
                                    break;
                                case CSSValue::Type::STRING:
                                    cv.string_value += val.string_value;
                                    break;
                                case CSSValue::Type::NUMBER:
                                    cv.string_value += std::to_string(val.number);
                                    break;
                                case CSSValue::Type::LENGTH: {
                                    cv.string_value += std::to_string(val.length.value);
                                    switch (val.length.unit) {
                                        case Length::Unit::PX:
                                            cv.string_value += "px";
                                            break;
                                        case Length::Unit::EM:
                                            cv.string_value += "em";
                                            break;
                                        case Length::Unit::REM:
                                            cv.string_value += "rem";
                                            break;
                                        case Length::Unit::PERCENT:
                                            cv.string_value += "%";
                                            break;
                                        case Length::Unit::VW:
                                            cv.string_value += "vw";
                                            break;
                                        case Length::Unit::VH:
                                            cv.string_value += "vh";
                                            break;
                                        default:
                                            break;
                                    }
                                    break;
                                }
                                case CSSValue::Type::COLOR: {
                                    char buf[16];
                                    snprintf(buf, sizeof(buf), "#%02x%02x%02x", val.color.r, val.color.g, val.color.b);
                                    cv.string_value += buf;
                                    break;
                                }
                                default:
                                    cv.string_value += val.keyword;
                                    break;
                            }
                        }
                        style.properties[prop] = cv;
                        continue;
                    }

                    if (md.decl->values.size() == 1) {
                        CSSValue val = md.decl->values[0];
                        if (val.type == CSSValue::Type::KEYWORD && val.keyword.substr(0, 4) == "var(") {
                            std::string resolved = resolve_var(val.keyword, style);
                            CSSValue cv;
                            if ((!resolved.empty() && std::isdigit(static_cast<unsigned char>(resolved[0]))) ||
                                (!resolved.empty() && (resolved[0] == '-' || resolved[0] == '+'))) {
                                char *end = nullptr;
                                f32 num = std::strtof(resolved.c_str(), &end);
                                if (end && end != resolved.c_str()) {
                                    std::string unit = end;
                                    if (unit == "px") {
                                        cv.type = CSSValue::Type::LENGTH;
                                        cv.length = {num, Length::Unit::PX};
                                    } else if (unit == "em") {
                                        cv.type = CSSValue::Type::LENGTH;
                                        cv.length = {num, Length::Unit::EM};
                                    } else if (unit == "%") {
                                        cv.type = CSSValue::Type::PERCENTAGE;
                                        cv.number = num;
                                    } else if (unit.empty()) {
                                        cv.type = CSSValue::Type::NUMBER;
                                        cv.number = num;
                                    } else {
                                        cv.type = CSSValue::Type::STRING;
                                        cv.string_value = resolved;
                                    }
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
                        } else if (val.type == CSSValue::Type::STRING &&
                                   val.string_value.find("var(") != std::string::npos) {
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
                            if (vi > 0)
                                combined.string_value += ' ';
                            const auto &val = md.decl->values[vi];
                            switch (val.type) {
                                case CSSValue::Type::KEYWORD:
                                    combined.string_value += val.keyword;
                                    break;
                                case CSSValue::Type::LENGTH: {
                                    std::string num = std::to_string(val.length.value);
                                    auto dot = num.find('.');
                                    if (dot != std::string::npos) {
                                        auto last = num.find_last_not_of('0');
                                        if (last > dot)
                                            num = num.substr(0, last + 1);
                                        else if (last == dot)
                                            num = num.substr(0, dot);
                                    }
                                    combined.string_value += num;
                                    switch (val.length.unit) {
                                        case Length::Unit::PX:
                                            combined.string_value += "px";
                                            break;
                                        case Length::Unit::EM:
                                            combined.string_value += "em";
                                            break;
                                        case Length::Unit::REM:
                                            combined.string_value += "rem";
                                            break;
                                        case Length::Unit::PERCENT:
                                            combined.string_value += "%";
                                            break;
                                        case Length::Unit::VW:
                                            combined.string_value += "vw";
                                            break;
                                        case Length::Unit::VH:
                                            combined.string_value += "vh";
                                            break;
                                        case Length::Unit::NONE:
                                            combined.string_value += "fr";
                                            break;
                                        case Length::Unit::DEG:
                                            combined.string_value += "deg";
                                            break;
                                        case Length::Unit::S:
                                            combined.string_value += "s";
                                            break;
                                        case Length::Unit::MS:
                                            combined.string_value += "ms";
                                            break;
                                        case Length::Unit::CH:
                                            combined.string_value += "ch";
                                            break;
                                        case Length::Unit::EX:
                                            combined.string_value += "ex";
                                            break;
                                        case Length::Unit::CM_UNIT:
                                            combined.string_value += "cm";
                                            break;
                                        case Length::Unit::MM_UNIT:
                                            combined.string_value += "mm";
                                            break;
                                        case Length::Unit::IN_UNIT:
                                            combined.string_value += "in";
                                            break;
                                        case Length::Unit::PT:
                                            combined.string_value += "pt";
                                            break;
                                        case Length::Unit::PC:
                                            combined.string_value += "pc";
                                            break;
                                        case Length::Unit::DPCM:
                                        case Length::Unit::DPI:
                                        case Length::Unit::FR:
                                            break;
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

                    {
                        const std::string &prop = md.decl->property;
                        CSSValue val = style.properties[md.decl->property];

                        if ((prop == "border" || prop == "border-top" || prop == "border-right" ||
                             prop == "border-bottom" || prop == "border-left") &&
                            md.decl->values.size() > 1) {
                            for (const auto &v : md.decl->values) {
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

                        auto expand_four_sides = [&](const std::string &base, const std::string &val_str) {
                            std::vector<std::string> parts;
                            std::string s = val_str;
                            size_t pp = 0;
                            while (pp < s.size()) {
                                while (pp < s.size() && s[pp] == ' ') pp++;
                                if (pp >= s.size())
                                    break;
                                size_t end = s.find(' ', pp);
                                if (end == std::string::npos)
                                    end = s.size();
                                parts.push_back(s.substr(pp, end - pp));
                                pp = end + 1;
                            }
                            auto set_side_fn = [&](const std::string &side, const std::string &pv) {
                                if (pv.empty())
                                    return;
                                CSSValue cv;
                                if (pv == "auto") {
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = "auto";
                                } else {
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = pv;
                                    char *endp = nullptr;
                                    f32 num = std::strtof(pv.c_str(), &endp);
                                    if (endp != pv.c_str()) {
                                        cv.type = CSSValue::Type::LENGTH;
                                        cv.length.value = num;
                                        std::string unit = endp;
                                        if (unit == "px")
                                            cv.length.unit = Length::Unit::PX;
                                        else if (unit == "em")
                                            cv.length.unit = Length::Unit::EM;
                                        else if (unit == "rem")
                                            cv.length.unit = Length::Unit::REM;
                                        else if (unit == "%")
                                            cv.length.unit = Length::Unit::PERCENT;
                                        else {
                                            cv.type = CSSValue::Type::STRING;
                                            cv.string_value = pv;
                                        }
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

                        if (prop == "margin") {
                            if (val.type == CSSValue::Type::STRING) {
                                expand_four_sides("margin", val.string_value);
                            } else {
                                // Single keyword/number value like "auto" or "0"
                                std::string val_str;
                                if (val.type == CSSValue::Type::KEYWORD)
                                    val_str = val.keyword;
                                else if (val.type == CSSValue::Type::LENGTH) {
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "%.0f", val.length.value);
                                    val_str = buf;
                                    if (val.length.unit == Length::Unit::PX)
                                        val_str += "px";
                                    else if (val.length.unit == Length::Unit::EM)
                                        val_str += "em";
                                    else if (val.length.unit == Length::Unit::REM)
                                        val_str += "rem";
                                    else if (val.length.unit == Length::Unit::PERCENT)
                                        val_str += "%";
                                } else if (val.type == CSSValue::Type::NUMBER) {
                                    val_str = std::to_string(val.number);
                                }
                                if (!val_str.empty())
                                    expand_four_sides("margin", val_str);
                            }
                        }
                        if (prop == "padding") {
                            if (val.type == CSSValue::Type::STRING) {
                                expand_four_sides("padding", val.string_value);
                            } else {
                                std::string val_str;
                                if (val.type == CSSValue::Type::KEYWORD)
                                    val_str = val.keyword;
                                else if (val.type == CSSValue::Type::LENGTH) {
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "%.0f", val.length.value);
                                    val_str = buf;
                                    if (val.length.unit == Length::Unit::PX)
                                        val_str += "px";
                                    else if (val.length.unit == Length::Unit::EM)
                                        val_str += "em";
                                    else if (val.length.unit == Length::Unit::REM)
                                        val_str += "rem";
                                    else if (val.length.unit == Length::Unit::PERCENT)
                                        val_str += "%";
                                }
                                if (!val_str.empty())
                                    expand_four_sides("padding", val_str);
                            }
                        }

                        auto expand_border_side = [&](const std::string &side, const CSSValue &bval) {
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
                            // Expand "border-width: 1px 2px" → border-top-width, border-right-width, etc.
                            std::string tmp = val.string_value;
                            val.string_value = tmp;
                            expand_four_sides("borderwidth", val.string_value);
                            // Rename borderwidth-top → border-top-width etc.
                            for (auto &side : {"-top", "-right", "-bottom", "-left"}) {
                                std::string old_key = "borderwidth" + std::string(side);
                                std::string new_key = "border" + std::string(side) + "-width";
                                auto it = style.properties.find(old_key);
                                if (it != style.properties.end()) {
                                    style.properties[new_key] = it->second;
                                    style.properties.erase(it);
                                }
                            }
                        }

                        if (prop == "flex" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            {
                                std::string trimmed = s;
                                while (!trimmed.empty() && trimmed[0] == ' ') trimmed = trimmed.substr(1);
                                while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
                                if (trimmed == "none") {
                                    auto set_flex_val = [&](const std::string &subprop, f32 num) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::NUMBER;
                                        cv.number = num;
                                        style.properties[subprop] = cv;
                                    };
                                    set_flex_val("flex-grow", 0);
                                    set_flex_val("flex-shrink", 0);
                                    {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = "auto";
                                        style.properties["flex-basis"] = cv;
                                    }
                                } else {
                                    std::vector<std::string> parts;
                                    size_t pp = 0;
                                    while (pp < s.size()) {
                                        while (pp < s.size() && s[pp] == ' ') pp++;
                                        if (pp >= s.size())
                                            break;
                                        size_t end = s.find(' ', pp);
                                        if (end == std::string::npos)
                                            end = s.size();
                                        parts.push_back(s.substr(pp, end - pp));
                                        pp = end + 1;
                                    }
                                    auto set_flex_num = [&](const std::string &subprop, const std::string &pv) {
                                        CSSValue cv;
                                        char *endp = nullptr;
                                        f32 num = std::strtof(pv.c_str(), &endp);
                                        if (endp != pv.c_str()) {
                                            cv.type = CSSValue::Type::NUMBER;
                                            cv.number = num;
                                            style.properties[subprop] = cv;
                                        }
                                    };
                                    auto set_flex_basis = [&](const std::string &pv) {
                                        CSSValue cv;
                                        char *endp = nullptr;
                                        f32 num = std::strtof(pv.c_str(), &endp);
                                        if (endp != pv.c_str()) {
                                            cv.type = CSSValue::Type::LENGTH;
                                            cv.length.value = num;
                                            std::string unit = endp;
                                            if (unit == "px")
                                                cv.length.unit = Length::Unit::PX;
                                            else if (unit == "em")
                                                cv.length.unit = Length::Unit::EM;
                                            else if (unit == "rem")
                                                cv.length.unit = Length::Unit::REM;
                                            else if (unit == "%")
                                                cv.length.unit = Length::Unit::PERCENT;
                                            else {
                                                cv.type = CSSValue::Type::KEYWORD;
                                                cv.keyword = pv;
                                            }
                                        } else {
                                            cv.type = CSSValue::Type::KEYWORD;
                                            cv.keyword = pv;
                                        }
                                        style.properties["flex-basis"] = cv;
                                    };
                                    if (parts.size() >= 1)
                                        set_flex_num("flex-grow", parts[0]);
                                    if (parts.size() >= 2)
                                        set_flex_num("flex-shrink", parts[1]);
                                    if (parts.size() >= 3)
                                        set_flex_basis(parts[2]);
                                    if (parts.size() == 1) {
                                        {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::NUMBER;
                                            cv.number = 1;
                                            style.properties["flex-shrink"] = cv;
                                        }
                                        {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::LENGTH;
                                            cv.length = {0, Length::Unit::PX};
                                            style.properties["flex-basis"] = cv;
                                        }
                                    }
                                    if (parts.size() == 2) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = "auto";
                                        if (!style.has("flex-basis"))
                                            style.properties["flex-basis"] = cv;
                                    }
                                }
                            }
                        }

                        if (prop == "background" &&
                            (val.type == CSSValue::Type::STRING || val.type == CSSValue::Type::COLOR)) {
                            if (!style.has("background-color"))
                                style.properties["background-color"] = val;
                        }

                        if (prop == "animation" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            size_t sp = 0;
                            std::vector<std::string> parts;
                            while (sp < s.size()) {
                                while (sp < s.size() && s[sp] == ' ') sp++;
                                if (sp >= s.size())
                                    break;
                                size_t end = s.find(' ', sp);
                                if (end == std::string::npos)
                                    end = s.size();
                                parts.push_back(s.substr(sp, end - sp));
                                sp = end + 1;
                            }

                            auto set_anim = [&](const std::string &subprop, const std::string &v) {
                                CSSValue cv;
                                cv.type = CSSValue::Type::KEYWORD;
                                cv.keyword = v;
                                style.properties[subprop] = cv;
                            };

                            int num_seen = 0;
                            for (const auto &p : parts) {
                                if (!p.empty() && (std::isdigit(static_cast<unsigned char>(p[0])) || p[0] == '.')) {
                                    char *end = nullptr;
                                    f32 num = std::strtof(p.c_str(), &end);
                                    if (end && *end != '\0') {
                                        if (num_seen == 0) {
                                            if (std::string(end) == "ms")
                                                set_anim("animation-duration", std::to_string(num / 1000.0f) + "s");
                                            else
                                                set_anim("animation-duration", p);
                                            num_seen++;
                                        }
                                    } else {
                                        num_seen++;
                                    }
                                }
                            }

                            if (!parts.empty() && parts[0] != "infinite") {
                                std::string first = parts[0];
                                if (first != "ease" && first != "linear" && first != "ease-in" && first != "ease-out" &&
                                    first != "ease-in-out" && first.substr(0, 6) != "cubic-" &&
                                    first.substr(0, 6) != "steps(" && first.find("ms") == std::string::npos &&
                                    first.find('s') == std::string::npos) {
                                    set_anim("animation-name", first);
                                }
                            }
                        }

                        // overflow: <value> → overflow-x + overflow-y
                        if (prop == "overflow" && val.type == CSSValue::Type::KEYWORD) {
                            if (!style.has("overflow-x"))
                                style.properties["overflow-x"] = val;
                            if (!style.has("overflow-y"))
                                style.properties["overflow-y"] = val;
                        }

                        // border-radius: <value> → four corners
                        if (prop == "border-radius") {
                            if (val.type == CSSValue::Type::LENGTH || val.type == CSSValue::Type::STRING) {
                                std::string val_str;
                                if (val.type == CSSValue::Type::LENGTH) {
                                    char buf[64];
                                    snprintf(buf, sizeof(buf), "%.2f", val.length.value);
                                    val_str = buf;
                                    if (val.length.unit == Length::Unit::PX) val_str += "px";
                                    else if (val.length.unit == Length::Unit::EM) val_str += "em";
                                    else if (val.length.unit == Length::Unit::REM) val_str += "rem";
                                    else if (val.length.unit == Length::Unit::PERCENT) val_str += "%";
                                } else {
                                    val_str = val.string_value;
                                }
                                if (!val_str.empty())
                                    expand_four_sides("border-radius", val_str);
                                // Rename border-radius-top → border-top-left-radius etc.
                                auto rename_br = [&](const std::string &old_key, const std::string &new_key) {
                                    auto it = style.properties.find(old_key);
                                    if (it != style.properties.end()) {
                                        style.properties[new_key] = it->second;
                                        style.properties.erase(it);
                                    }
                                };
                                rename_br("border-radius-top", "border-top-left-radius");
                                rename_br("border-radius-right", "border-top-right-radius");
                                rename_br("border-radius-bottom", "border-bottom-right-radius");
                                rename_br("border-radius-left", "border-bottom-left-radius");
                            }
                        }

                        // outline: <width> <style> <color>
                        if (prop == "outline" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                if (p == "none" || p == "dotted" || p == "dashed" || p == "solid" ||
                                    p == "double" || p == "groove" || p == "ridge" || p == "inset" || p == "outset") {
                                    if (!style.has("outline-style")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["outline-style"] = cv;
                                    }
                                } else if (p == "invert") {
                                    if (!style.has("outline-color")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["outline-color"] = cv;
                                    }
                                } else {
                                    char *endp = nullptr;
                                    f32 num = std::strtof(p.c_str(), &endp);
                                    if (endp != p.c_str()) {
                                        std::string unit = endp;
                                        if (!style.has("outline-width")) {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::LENGTH;
                                            cv.length.value = num;
                                            if (unit == "px") cv.length.unit = Length::Unit::PX;
                                            else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                            else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                            else cv.length.unit = Length::Unit::PX;
                                            style.properties["outline-width"] = cv;
                                        }
                                    } else if (!style.has("outline-color")) {
                                        auto named = Color::from_name(p);
                                        if (named.a != 0 || p == "transparent") {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::COLOR;
                                            cv.color = named;
                                            style.properties["outline-color"] = cv;
                                        } else {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::KEYWORD;
                                            cv.keyword = p;
                                            style.properties["outline-color"] = cv;
                                        }
                                    }
                                }
                            }
                        }

                        // text-decoration: <line> <style> <color>
                        if (prop == "text-decoration" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                if (p == "underline" || p == "overline" || p == "line-through" || p == "none") {
                                    if (!style.has("text-decoration-line")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["text-decoration-line"] = cv;
                                    }
                                } else if (p == "solid" || p == "double" || p == "dotted" || p == "dashed" || p == "wavy") {
                                    if (!style.has("text-decoration-style")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["text-decoration-style"] = cv;
                                    }
                                } else if (!style.has("text-decoration-color")) {
                                    auto named = Color::from_name(p);
                                    if (named.a != 0 || p == "transparent") {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::COLOR;
                                        cv.color = named;
                                        style.properties["text-decoration-color"] = cv;
                                    }
                                }
                            }
                        }

                        // flex-flow: <direction> <wrap>
                        if (prop == "flex-flow" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                if (p == "row" || p == "row-reverse" || p == "column" || p == "column-reverse") {
                                    if (!style.has("flex-direction")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["flex-direction"] = cv;
                                    }
                                } else if (p == "wrap" || p == "wrap-reverse" || p == "nowrap") {
                                    if (!style.has("flex-wrap")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["flex-wrap"] = cv;
                                    }
                                }
                            }
                        }

                        // transition: <property> <duration> <timing> <delay>
                        if (prop == "transition" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            int time_count = 0;
                            for (const auto &p : parts) {
                                if (!p.empty() && (std::isdigit(static_cast<unsigned char>(p[0])) || p[0] == '.')) {
                                    char *end = nullptr;
                                    std::strtof(p.c_str(), &end);
                                    if (end && *end != '\0') {
                                        // Has a unit — it's a time value
                                        if (time_count == 0) {
                                            if (!style.has("transition-duration")) {
                                                CSSValue cv;
                                                cv.type = CSSValue::Type::KEYWORD;
                                                cv.keyword = p;
                                                style.properties["transition-duration"] = cv;
                                            }
                                        } else if (time_count == 1) {
                                            if (!style.has("transition-delay")) {
                                                CSSValue cv;
                                                cv.type = CSSValue::Type::KEYWORD;
                                                cv.keyword = p;
                                                style.properties["transition-delay"] = cv;
                                            }
                                        }
                                        time_count++;
                                    }
                                } else if (p == "ease" || p == "linear" || p == "ease-in" || p == "ease-out" || p == "ease-in-out") {
                                    if (!style.has("transition-timing-function")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["transition-timing-function"] = cv;
                                    }
                                } else {
                                    // Assume it's the property name
                                    if (!style.has("transition-property")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["transition-property"] = cv;
                                    }
                                }
                            }
                        }

                        // list-style: <type> <position> <image>
                        if (prop == "list-style" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                if (p == "inside" || p == "outside") {
                                    if (!style.has("list-style-position")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["list-style-position"] = cv;
                                    }
                                } else if (p.substr(0, 4) == "url(" || p == "none") {
                                    if (!style.has("list-style-image")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["list-style-image"] = cv;
                                    }
                                } else {
                                    if (!style.has("list-style-type")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["list-style-type"] = cv;
                                    }
                                }
                            }
                        }

                        // gap: <row-gap> <column-gap>
                        if (prop == "gap" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            auto parse_gap_val = [&](const std::string &pv) -> CSSValue {
                                CSSValue cv;
                                char *endp = nullptr;
                                f32 num = std::strtof(pv.c_str(), &endp);
                                if (endp != pv.c_str()) {
                                    cv.type = CSSValue::Type::LENGTH;
                                    cv.length.value = num;
                                    std::string unit = endp;
                                    if (unit == "px") cv.length.unit = Length::Unit::PX;
                                    else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                    else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                    else if (unit == "%") cv.length.unit = Length::Unit::PERCENT;
                                    else cv.length.unit = Length::Unit::PX;
                                } else {
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = pv;
                                }
                                return cv;
                            };
                            if (!parts.empty()) {
                                if (!style.has("row-gap"))
                                    style.properties["row-gap"] = parse_gap_val(parts[0]);
                                if (!style.has("column-gap"))
                                    style.properties["column-gap"] = parts.size() > 1 ? parse_gap_val(parts[1]) : parse_gap_val(parts[0]);
                            }
                        }
                        if (prop == "gap" && val.type == CSSValue::Type::LENGTH) {
                            if (!style.has("row-gap"))
                                style.properties["row-gap"] = val;
                            if (!style.has("column-gap"))
                                style.properties["column-gap"] = val;
                        }

                        // place-content: <align-content> <justify-content>
                        if (prop == "place-content" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("align-content")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(0, sp);
                                    style.properties["align-content"] = cv;
                                }
                                if (!style.has("justify-content")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(sp + 1);
                                    style.properties["justify-content"] = cv;
                                }
                            } else {
                                if (!style.has("align-content")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["align-content"] = cv;
                                }
                                if (!style.has("justify-content")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["justify-content"] = cv;
                                }
                            }
                        }

                        // place-items: <align-items> <justify-items>
                        if (prop == "place-items" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("align-items")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(0, sp);
                                    style.properties["align-items"] = cv;
                                }
                                if (!style.has("justify-items")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(sp + 1);
                                    style.properties["justify-items"] = cv;
                                }
                            } else {
                                if (!style.has("align-items")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["align-items"] = cv;
                                }
                                if (!style.has("justify-items")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["justify-items"] = cv;
                                }
                            }
                        }

                        // place-self: <align-self> <justify-self>
                        if (prop == "place-self" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("align-self")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(0, sp);
                                    style.properties["align-self"] = cv;
                                }
                                if (!style.has("justify-self")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s.substr(sp + 1);
                                    style.properties["justify-self"] = cv;
                                }
                            } else {
                                if (!style.has("align-self")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["align-self"] = cv;
                                }
                                if (!style.has("justify-self")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::KEYWORD;
                                    cv.keyword = s;
                                    style.properties["justify-self"] = cv;
                                }
                            }
                        }

                        // inset: <top> <right> <bottom> <left>
                        if (prop == "inset" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            expand_four_sides("inset", s);
                            auto rename_inset = [&](const std::string &old_key, const std::string &new_key) {
                                auto it = style.properties.find(old_key);
                                if (it != style.properties.end()) {
                                    style.properties[new_key] = it->second;
                                    style.properties.erase(it);
                                }
                            };
                            rename_inset("inset-top", "top");
                            rename_inset("inset-right", "right");
                            rename_inset("inset-bottom", "bottom");
                            rename_inset("inset-left", "left");
                        }

                        // columns: <column-width> <column-count>
                        if (prop == "columns" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                char *endp = nullptr;
                                f32 num = std::strtof(p.c_str(), &endp);
                                if (endp != p.c_str()) {
                                    std::string unit = endp;
                                    if (unit == "auto" || p == "auto") {
                                        // auto width or count
                                    } else if (unit.empty()) {
                                        // Bare number = column-count
                                        if (!style.has("column-count")) {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::NUMBER;
                                            cv.number = num;
                                            style.properties["column-count"] = cv;
                                        }
                                    } else {
                                        // Has unit = column-width
                                        if (!style.has("column-width")) {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::LENGTH;
                                            cv.length.value = num;
                                            if (unit == "px") cv.length.unit = Length::Unit::PX;
                                            else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                            else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                            else cv.length.unit = Length::Unit::PX;
                                            style.properties["column-width"] = cv;
                                        }
                                    }
                                }
                            }
                        }

                        // column-rule: <width> <style> <color>
                        if (prop == "column-rule" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
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
                            for (const auto &p : parts) {
                                if (p == "none" || p == "solid" || p == "dotted" || p == "dashed" || p == "double") {
                                    if (!style.has("column-rule-style")) {
                                        CSSValue cv;
                                        cv.type = CSSValue::Type::KEYWORD;
                                        cv.keyword = p;
                                        style.properties["column-rule-style"] = cv;
                                    }
                                } else {
                                    char *endp = nullptr;
                                    f32 num = std::strtof(p.c_str(), &endp);
                                    if (endp != p.c_str()) {
                                        if (!style.has("column-rule-width")) {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::LENGTH;
                                            cv.length.value = num;
                                            cv.length.unit = Length::Unit::PX;
                                            style.properties["column-rule-width"] = cv;
                                        }
                                    } else if (!style.has("column-rule-color")) {
                                        auto named = Color::from_name(p);
                                        if (named.a != 0 || p == "transparent") {
                                            CSSValue cv;
                                            cv.type = CSSValue::Type::COLOR;
                                            cv.color = named;
                                            style.properties["column-rule-color"] = cv;
                                        }
                                    }
                                }
                            }
                        }

                        // grid-row: <start> / <end>
                        if (prop == "grid-row" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto slash = s.find('/');
                            if (slash != std::string::npos) {
                                std::string start = s.substr(0, slash);
                                std::string end = s.substr(slash + 1);
                                while (!start.empty() && start.back() == ' ') start.pop_back();
                                while (!end.empty() && end[0] == ' ') end = end.substr(1);
                                if (!style.has("grid-row-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = start;
                                    style.properties["grid-row-start"] = cv;
                                }
                                if (!style.has("grid-row-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = end;
                                    style.properties["grid-row-end"] = cv;
                                }
                            }
                        }

                        // grid-column: <start> / <end>
                        if (prop == "grid-column" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto slash = s.find('/');
                            if (slash != std::string::npos) {
                                std::string start = s.substr(0, slash);
                                std::string end = s.substr(slash + 1);
                                while (!start.empty() && start.back() == ' ') start.pop_back();
                                while (!end.empty() && end[0] == ' ') end = end.substr(1);
                                if (!style.has("grid-column-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = start;
                                    style.properties["grid-column-start"] = cv;
                                }
                                if (!style.has("grid-column-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = end;
                                    style.properties["grid-column-end"] = cv;
                                }
                            }
                        }

                        // grid-area: <row-start> / <col-start> / <row-end> / <col-end>
                        if (prop == "grid-area" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            std::vector<std::string> parts;
                            size_t pp = 0;
                            while (pp < s.size()) {
                                while (pp < s.size() && s[pp] == ' ') pp++;
                                if (pp >= s.size()) break;
                                size_t end = s.find('/', pp);
                                if (end == std::string::npos) end = s.size();
                                std::string part = s.substr(pp, end - pp);
                                while (!part.empty() && part.back() == ' ') part.pop_back();
                                if (!part.empty()) parts.push_back(part);
                                if (end != std::string::npos) pp = end + 1; else break;
                            }
                            auto set_grid_area = [&](const std::string &subprop, const std::string &v) {
                                if (!style.has(subprop)) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = v;
                                    style.properties[subprop] = cv;
                                }
                            };
                            if (parts.size() >= 1) set_grid_area("grid-row-start", parts[0]);
                            if (parts.size() >= 2) set_grid_area("grid-column-start", parts[1]);
                            if (parts.size() >= 3) set_grid_area("grid-row-end", parts[2]);
                            if (parts.size() >= 4) set_grid_area("grid-column-end", parts[3]);
                        }

                        // scroll-margin: <four-sides>
                        if (prop == "scroll-margin" && val.type == CSSValue::Type::STRING) {
                            expand_four_sides("scroll-margin", val.string_value);
                        }
                        // scroll-padding: <four-sides>
                        if (prop == "scroll-padding" && val.type == CSSValue::Type::STRING) {
                            expand_four_sides("scroll-padding", val.string_value);
                        }

                        // margin-block: <start> <end>
                        if (prop == "margin-block" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("margin-block-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(0, sp);
                                    style.properties["margin-block-start"] = cv;
                                }
                                if (!style.has("margin-block-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(sp + 1);
                                    style.properties["margin-block-end"] = cv;
                                }
                            } else {
                                if (!style.has("margin-block-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["margin-block-start"] = cv;
                                }
                                if (!style.has("margin-block-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["margin-block-end"] = cv;
                                }
                            }
                        }

                        // margin-inline: <start> <end>
                        if (prop == "margin-inline" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("margin-inline-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(0, sp);
                                    style.properties["margin-inline-start"] = cv;
                                }
                                if (!style.has("margin-inline-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(sp + 1);
                                    style.properties["margin-inline-end"] = cv;
                                }
                            } else {
                                if (!style.has("margin-inline-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["margin-inline-start"] = cv;
                                }
                                if (!style.has("margin-inline-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["margin-inline-end"] = cv;
                                }
                            }
                        }

                        // padding-block: <start> <end>
                        if (prop == "padding-block" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("padding-block-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(0, sp);
                                    style.properties["padding-block-start"] = cv;
                                }
                                if (!style.has("padding-block-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(sp + 1);
                                    style.properties["padding-block-end"] = cv;
                                }
                            } else {
                                if (!style.has("padding-block-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["padding-block-start"] = cv;
                                }
                                if (!style.has("padding-block-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["padding-block-end"] = cv;
                                }
                            }
                        }

                        // padding-inline: <start> <end>
                        if (prop == "padding-inline" && val.type == CSSValue::Type::STRING) {
                            std::string s = val.string_value;
                            auto sp = s.find(' ');
                            if (sp != std::string::npos) {
                                if (!style.has("padding-inline-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(0, sp);
                                    style.properties["padding-inline-start"] = cv;
                                }
                                if (!style.has("padding-inline-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s.substr(sp + 1);
                                    style.properties["padding-inline-end"] = cv;
                                }
                            } else {
                                if (!style.has("padding-inline-start")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["padding-inline-start"] = cv;
                                }
                                if (!style.has("padding-inline-end")) {
                                    CSSValue cv;
                                    cv.type = CSSValue::Type::STRING;
                                    cv.string_value = s;
                                    style.properties["padding-inline-end"] = cv;
                                }
                            }
                        }
                    }
                }
            }

            styles[el] = std::move(style);
        });

        // Second pass: set parent pointers now that the map is stable (won't rehash)
        for (auto &[el, style] : styles) {
            if (el->parent && el->parent->type == html::NodeType::ELEMENT) {
                auto *parent_el = static_cast<const html::Element *>(el->parent);
                auto pit = styles.find(const_cast<html::Element *>(parent_el));
                if (pit != styles.end()) {
                    style.parent = &pit->second;
                }
            }
        }

        co_return CascadeResult{std::move(styles)};
    }

}  // namespace browser::css
