#include "cascade.hpp"
#include "parser.hpp"
#include "selector_match.hpp"
#include "../html/traversal.hpp"
#include <algorithm>
#include <cstdlib>

namespace browser::css {

static constexpr const char* UA_STYLESHEET = R"(
body { display: block; margin: 8px; }
div, p, h1, h2, h3, h4, h5, h6, ul, ol, li, table { display: block; }
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

bool is_inherited(const std::string& property) {
    return property == "color" ||
           property == "font-size" ||
           property == "font-family" ||
           property == "font-weight" ||
           property == "font-style" ||
           property == "line-height" ||
           property == "visibility" ||
           property == "cursor";
}

std::unordered_map<const html::Element*, ComputedStyle>
Cascade::compute(const html::Document& doc, const StyleSheet& author) {
    CssParser ua_parser(UA_STYLESHEET);
    StyleSheet ua = ua_parser.parse();

    std::unordered_map<const html::Element*, std::vector<MatchedDecl>> matched;
    u32 source_order = 0;

    // Phase 1: Collect
    auto* doc_node = const_cast<html::Document*>(&doc);
    html::traverse_depth_first(doc_node, [&](html::Node* node) {
        if (node->type != html::NodeType::ELEMENT) return;
        auto* el = static_cast<html::Element*>(node);

        std::vector<MatchedDecl> decls;

        // Collect rules from a stylesheet (including @media)
        for (u8 origin = 0; origin <= 1; origin++) {
            const StyleSheet* sheet = (origin == 0) ? &ua : &author;
            for (const auto& rule : sheet->rules) {
                for (const auto& sel : rule.selectors) {
                    if (matches_selector(sel, el, &doc)) {
                        for (const auto& decl : rule.declarations) {
                            decls.push_back({&decl, compute_specificity(sel), source_order++, origin});
                        }
                        break;
                    }
                }
            }
            for (const auto& at : sheet->at_rules) {
                if (at.name == "media") {
                    for (const auto& rule : at.rules) {
                        for (const auto& sel : rule.selectors) {
                            if (matches_selector(sel, el, &doc)) {
                                for (const auto& decl : rule.declarations) {
                                    decls.push_back({&decl, compute_specificity(sel), source_order++, origin});
                                }
                                break;
                            }
                        }
                    }
                }
                for (const auto& nested : at.at_rules) {
                    if (nested.name == "media") {
                        for (const auto& rule : nested.rules) {
                            for (const auto& sel : rule.selectors) {
                                if (matches_selector(sel, el, &doc)) {
                                    for (const auto& decl : rule.declarations) {
                                        decls.push_back({&decl, compute_specificity(sel), source_order++, origin});
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Collect inline style from element's style attribute (origin=2, highest)
        std::string inline_style = el->get_attribute("style");
        if (!inline_style.empty()) {
            CssParser inline_parser(inline_style);
            StyleSheet inline_sheet = inline_parser.parse();
            for (const auto& rule : inline_sheet.rules) {
                for (const auto& decl : rule.declarations) {
                    Specificity spec;
                    spec.bits = 0;
                    decls.push_back({&decl, spec, source_order++, 2});
                }
            }
        }

        matched[el] = std::move(decls);
    });

    // Phase 2: Sort (origin: 0=UA, 1=author, 2=inline; !important flips the order)
    for (auto& [el, decls] : matched) {
        std::sort(decls.begin(), decls.end(), [](const MatchedDecl& a, const MatchedDecl& b) {
            // !important declarations win regardless of origin
            bool a_imp = a.decl->important;
            bool b_imp = b.decl->important;
            if (a_imp != b_imp) return a_imp > b_imp;
            // For non-important: 0=UA, 1=author, 2=inline (higher wins)
            if (!a_imp) {
                if (a.origin != b.origin) return a.origin < b.origin;
            } else {
                // For important: reverse order — author !important beats UA !important
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
                if (md.decl->values.size() == 1) {
                    style.properties[md.decl->property] = md.decl->values[0];
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
                        }
                    }
                    style.properties[md.decl->property] = std::move(combined);
                }
                // Expand shorthands into longhands
                {
                    const std::string& prop = md.decl->property;
                    const CSSValue& val = style.properties[md.decl->property];
                    if (prop == "margin" && val.type == CSSValue::Type::STRING) {
                        std::vector<std::string> parts;
                        std::string s = val.string_value;
                        size_t pp = 0;
                        while (pp < s.size()) {
                            while (pp < s.size() && s[pp] == ' ') pp++;
                            if (pp >= s.size()) break;
                            size_t end = s.find(' ', pp);
                            if (end == std::string::npos) end = s.size();
                            parts.push_back(s.substr(pp, end - pp));
                            pp = end + 1;
                        }
                        auto set_side = [&](const std::string& side, const std::string& pv) {
                            if (pv.empty() || style.has(side)) return;
                            CSSValue cv;
                            if (pv == "auto") {
                                cv.type = CSSValue::Type::KEYWORD;
                                cv.keyword = "auto";
                            } else {
                                cv.type = CSSValue::Type::STRING;
                                cv.string_value = pv;
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
                                    else { cv.type = CSSValue::Type::STRING; cv.string_value = pv; }
                                }
                            }
                            style.properties[side] = cv;
                        };
                        auto set_side_fn = set_side;
                        if (parts.size() == 1) {
                            set_side_fn("margin-top", parts[0]);
                            set_side_fn("margin-right", parts[0]);
                            set_side_fn("margin-bottom", parts[0]);
                            set_side_fn("margin-left", parts[0]);
                        } else if (parts.size() == 2) {
                            set_side_fn("margin-top", parts[0]);
                            set_side_fn("margin-right", parts[1]);
                            set_side_fn("margin-bottom", parts[0]);
                            set_side_fn("margin-left", parts[1]);
                        } else if (parts.size() == 3) {
                            set_side_fn("margin-top", parts[0]);
                            set_side_fn("margin-right", parts[1]);
                            set_side_fn("margin-bottom", parts[2]);
                            set_side_fn("margin-left", parts[1]);
                        } else if (parts.size() == 4) {
                            set_side_fn("margin-top", parts[0]);
                            set_side_fn("margin-right", parts[1]);
                            set_side_fn("margin-bottom", parts[2]);
                            set_side_fn("margin-left", parts[3]);
                        }
                    }
                    if (prop == "padding" && val.type == CSSValue::Type::STRING) {
                        std::vector<std::string> parts;
                        std::string s = val.string_value;
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
                            CSSValue cv; cv.type = CSSValue::Type::STRING; cv.string_value = pv;
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
                                else { cv.type = CSSValue::Type::STRING; cv.string_value = pv; }
                            }
                            style.properties[side] = cv;
                        };
                        if (parts.size() == 1) {
                            set_side_fn("padding-top", parts[0]);
                            set_side_fn("padding-right", parts[0]);
                            set_side_fn("padding-bottom", parts[0]);
                            set_side_fn("padding-left", parts[0]);
                        } else if (parts.size() == 2) {
                            set_side_fn("padding-top", parts[0]);
                            set_side_fn("padding-right", parts[1]);
                            set_side_fn("padding-bottom", parts[0]);
                            set_side_fn("padding-left", parts[1]);
                        } else if (parts.size() == 4) {
                            set_side_fn("padding-top", parts[0]);
                            set_side_fn("padding-right", parts[1]);
                            set_side_fn("padding-bottom", parts[2]);
                            set_side_fn("padding-left", parts[3]);
                        }
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
                    if ((prop == "border-top" || prop == "border-right" ||
                         prop == "border-bottom" || prop == "border-left") &&
                        val.type == CSSValue::Type::STRING) {
                        expand_border_side(prop, val);
                    }
                    if (prop == "border-width" && val.type == CSSValue::Type::STRING) {
                        // Expand 1-4 values similar to margin/padding
                        std::vector<std::string> parts;
                        std::string s = val.string_value;
                        size_t pp = 0;
                        while (pp < s.size()) {
                            while (pp < s.size() && s[pp] == ' ') pp++;
                            if (pp >= s.size()) break;
                            size_t end = s.find(' ', pp);
                            if (end == std::string::npos) end = s.size();
                            parts.push_back(s.substr(pp, end - pp));
                            pp = end + 1;
                        }
                        auto set_bw = [&](const std::string& side, const std::string& pv) {
                            if (pv.empty() || style.has(side)) return;
                            CSSValue cv; cv.type = CSSValue::Type::STRING; cv.string_value = pv;
                            char* endp = nullptr;
                            f32 num = std::strtof(pv.c_str(), &endp);
                            if (endp != pv.c_str()) {
                                cv.type = CSSValue::Type::LENGTH; cv.length.value = num;
                                std::string unit = endp;
                                if (unit == "px") cv.length.unit = Length::Unit::PX;
                                else if (unit == "em") cv.length.unit = Length::Unit::EM;
                                else if (unit == "rem") cv.length.unit = Length::Unit::REM;
                                else { cv.type = CSSValue::Type::STRING; cv.string_value = pv; }
                            }
                            style.properties[side] = cv;
                        };
                        if (parts.size() == 1) {
                            set_bw("border-top-width", parts[0]); set_bw("border-right-width", parts[0]);
                            set_bw("border-bottom-width", parts[0]); set_bw("border-left-width", parts[0]);
                        } else if (parts.size() == 2) {
                            set_bw("border-top-width", parts[0]); set_bw("border-right-width", parts[1]);
                            set_bw("border-bottom-width", parts[0]); set_bw("border-left-width", parts[1]);
                        } else if (parts.size() == 4) {
                            set_bw("border-top-width", parts[0]); set_bw("border-right-width", parts[1]);
                            set_bw("border-bottom-width", parts[2]); set_bw("border-left-width", parts[3]);
                        }
                    }
                    if (prop == "background" && val.type == CSSValue::Type::STRING) {
                        if (!style.has("background-color"))
                            style.properties["background-color"] = val;
                    }
                }
            }
        }

        styles[el] = std::move(style);
    });

    // Relink parent pointers after map is finalized
    // (the map may reallocate during insertion, invalidating pointers)
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

    return styles;
}

}
