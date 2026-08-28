#include "css_dump.hpp"
#include "json_writer.hpp"
#include "../../core/utility.hpp"
#include "../../css/parser.hpp"
#include "../../css/css_values.hpp"
#include <string>

using browser::f32;

// ---------------------------------------------------------------------------
// CSS dump
// ---------------------------------------------------------------------------
std::string css_val_type_str(browser::css::CSSValue::Type t) {
    switch (t) {
        case browser::css::CSSValue::Type::KEYWORD:
            return "KEYWORD";
        case browser::css::CSSValue::Type::LENGTH:
            return "LENGTH";
        case browser::css::CSSValue::Type::COLOR:
            return "COLOR";
        case browser::css::CSSValue::Type::STRING:
            return "STRING";
        case browser::css::CSSValue::Type::NUMBER:
            return "NUMBER";
        case browser::css::CSSValue::Type::PERCENTAGE:
            return "PERCENTAGE";
        case browser::css::CSSValue::Type::URL:
            return "URL";
        case browser::css::CSSValue::Type::FUNCTION:
            return "FUNCTION";
        case browser::css::CSSValue::Type::GRADIENT:
            return "GRADIENT";
        case browser::css::CSSValue::Type::TRANSFORM:
            return "TRANSFORM";
        case browser::css::CSSValue::Type::SHADOW_LIST:
            return "SHADOW_LIST";
        case browser::css::CSSValue::Type::FILTER_LIST:
            return "FILTER_LIST";
        case browser::css::CSSValue::Type::TRANSITION_LIST:
            return "TRANSITION_LIST";
    }
    return "UNKNOWN";
}

std::string dump_declaration(const browser::css::Declaration &decl) {
    json::Obj o;
    o.kv_raw("property", json::q(decl.property));
    json::Arr vals;
    for (auto &v : decl.values) {
        json::Obj vobj;
        vobj.kv_raw("type", json::q(css_val_type_str(v.type)));
        if (v.type == browser::css::CSSValue::Type::KEYWORD)
            vobj.kv_raw("keyword", json::q(v.keyword));
        else if (v.type == browser::css::CSSValue::Type::LENGTH) {
            vobj.kv_num("value", v.length.value);
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
                default:
                    u = "px";
                    break;
            }
            vobj.kv_raw("unit", json::q(u));
        } else if (v.type == browser::css::CSSValue::Type::COLOR) {
            vobj.kv_num("r", static_cast<f32>(v.color.r));
            vobj.kv_num("g", static_cast<f32>(v.color.g));
            vobj.kv_num("b", static_cast<f32>(v.color.b));
            vobj.kv_num("a", static_cast<f32>(v.color.a));
        } else if (v.type == browser::css::CSSValue::Type::NUMBER ||
                   v.type == browser::css::CSSValue::Type::PERCENTAGE) {
            vobj.kv_num("number", v.number);
        } else if (v.type == browser::css::CSSValue::Type::STRING) {
            vobj.kv_raw("string_value", json::q(v.string_value));
        } else if (v.type == browser::css::CSSValue::Type::FUNCTION) {
            vobj.kv_raw("string_value", json::q(v.string_value));
        }
        vals.push(vobj.done());
    }
    o.kv_raw("values", vals.done());
    o.kv_bool("important", decl.important);
    return o.done();
}

std::string dump_rule(const browser::css::Rule &rule, int idx) {
    json::Obj o;
    o.kv("type", "rule");
    json::Arr sels;
    for (auto &sel : rule.selectors) {
        std::string sel_str;
        for (size_t i = 0; i < sel.compounds.size(); i++) {
            if (i > 0) {
                if (i - 1 < sel.combinators.size()) {
                    switch (sel.combinators[i - 1]) {
                        case browser::css::Combinator::DESCENDANT:
                            sel_str += " ";
                            break;
                        case browser::css::Combinator::CHILD:
                            sel_str += " > ";
                            break;
                        case browser::css::Combinator::ADJACENT_SIBLING:
                            sel_str += " + ";
                            break;
                        case browser::css::Combinator::GENERAL_SIBLING:
                            sel_str += " ~ ";
                            break;
                    }
                } else
                    sel_str += " ";
            }
            for (auto &ss : sel.compounds[i].simples) {
                switch (ss.type) {
                    case browser::css::SimpleSelector::Type::TAG:
                        sel_str += ss.name;
                        break;
                    case browser::css::SimpleSelector::Type::CLASS:
                        sel_str += "." + ss.name;
                        break;
                    case browser::css::SimpleSelector::Type::ID:
                        sel_str += "#" + ss.name;
                        break;
                    case browser::css::SimpleSelector::Type::UNIVERSAL:
                        sel_str += "*";
                        break;
                    case browser::css::SimpleSelector::Type::ATTRIBUTE: {
                        sel_str += "[" + ss.name;
                        if (ss.match_operator) {
                            if (ss.match_operator != '=')
                                sel_str += ss.match_operator;
                            sel_str += "=" + ss.value;
                        }
                        sel_str += "]";
                        break;
                    }
                    case browser::css::SimpleSelector::Type::PSEUDO_CLASS: {
                        if (ss.name == "nth-child" || ss.name == "nth-last-child") {
                            sel_str += ":" + ss.name + "(";
                            if (ss.nth_args.is_odd)
                                sel_str += "odd";
                            else if (ss.nth_args.is_even)
                                sel_str += "even";
                            else if (ss.nth_args.a == 0)
                                sel_str += std::to_string(ss.nth_args.b);
                            else {
                                sel_str += std::to_string(ss.nth_args.a) + "n";
                                if (ss.nth_args.b > 0)
                                    sel_str += "+" + std::to_string(ss.nth_args.b);
                                else if (ss.nth_args.b < 0)
                                    sel_str += std::to_string(ss.nth_args.b);
                            }
                            sel_str += ")";
                        } else {
                            sel_str += ":" + ss.name;
                            if (!ss.argument_selectors.empty()) {
                                sel_str += "(";
                                for (size_t ai = 0; ai < ss.argument_selectors.size(); ai++) {
                                    if (ai > 0)
                                        sel_str += ",";
                                    for (auto &cc : ss.argument_selectors[ai].compounds)
                                        for (auto &sss : cc.simples) {
                                            if (sss.type == browser::css::SimpleSelector::Type::TAG)
                                                sel_str += sss.name;
                                            else if (sss.type == browser::css::SimpleSelector::Type::CLASS)
                                                sel_str += "." + sss.name;
                                            else if (sss.type == browser::css::SimpleSelector::Type::ID)
                                                sel_str += "#" + sss.name;
                                            else if (sss.type == browser::css::SimpleSelector::Type::UNIVERSAL)
                                                sel_str += "*";
                                            else if (sss.type == browser::css::SimpleSelector::Type::ATTRIBUTE) {
                                                sel_str += "[" + sss.name;
                                                if (sss.match_operator) {
                                                    if (sss.match_operator != '=')
                                                        sel_str += sss.match_operator;
                                                    sel_str += "=\"" + sss.value + "\"";
                                                }
                                                sel_str += "]";
                                            } else if (sss.type == browser::css::SimpleSelector::Type::PSEUDO_CLASS) {
                                                sel_str += ":" + sss.name;
                                                if (!sss.argument_selectors.empty()) {
                                                    sel_str += "(";
                                                    for (size_t ai2 = 0; ai2 < sss.argument_selectors.size(); ai2++) {
                                                        if (ai2 > 0)
                                                            sel_str += ",";
                                                        for (auto &cc2 : sss.argument_selectors[ai2].compounds)
                                                            for (auto &sss2 : cc2.simples)
                                                                if (sss2.type ==
                                                                    browser::css::SimpleSelector::Type::TAG)
                                                                    sel_str += sss2.name;
                                                    }
                                                    sel_str += ")";
                                                }
                                            }
                                        }
                                }
                                sel_str += ")";
                            }
                        }
                        break;
                    }
                    case browser::css::SimpleSelector::Type::PSEUDO_ELEMENT:
                        sel_str += "::" + ss.name;
                        break;
                }
            }
        }
        sels.push(json::q(sel_str));
    }
    o.kv_raw("selectors", sels.done());
    json::Arr decls;
    for (auto &decl : rule.declarations) decls.push(dump_declaration(decl));
    o.kv_raw("declarations", decls.done());
    o.kv_raw("source_index", std::to_string(idx));
    return o.done();
}

std::string dump_stylesheet(const browser::css::StyleSheet &sheet) {
    json::Arr rules;
    for (size_t i = 0; i < sheet.rules.size(); i++) rules.push(dump_rule(sheet.rules[i], static_cast<int>(i)));
    json::Arr atrules;
    for (auto &at : sheet.at_rules) {
        json::Obj a;
        a.kv_raw("name", json::q(at.name));
        a.kv_raw("prelude", json::q(at.prelude));
        json::Arr nested;
        for (auto &r : at.rules) nested.push(dump_rule(r, 0));
        a.kv_raw("rules", nested.done());
        json::Arr nested_at;
        for (auto &na : at.at_rules) {
            json::Obj nao;
            nao.kv_raw("name", json::q(na.name));
            nao.kv_raw("prelude", json::q(na.prelude));
            nao.kv_raw("rules", json::Arr().done());
            nao.kv_raw("at_rules", json::Arr().done());
            nested_at.push(nao.done());
        }
        a.kv_raw("at_rules", nested_at.done());
        if (at.name == "keyframes" || at.name == "-webkit-keyframes") {
            json::Obj kf;
            kf.kv_raw("name", json::q(at.keyframes.name));
            json::Arr blks;
            for (auto &blk : at.keyframes.blocks) {
                json::Obj b;
                json::Arr pos;
                for (auto p : blk.positions) pos.push(json::num(p));
                b.kv_raw("positions", pos.done());
                json::Arr dcl;
                for (auto &d : blk.declarations) dcl.push(dump_declaration(d));
                b.kv_raw("declarations", dcl.done());
                blks.push(b.done());
            }
            kf.kv_raw("blocks", blks.done());
            a.kv_raw("keyframes", kf.done());
        }
        atrules.push(a.done());
    }
    json::Obj out;
    out.kv_raw("rules", rules.done());
    out.kv_raw("at_rules", atrules.done());
    return out.done();
}

