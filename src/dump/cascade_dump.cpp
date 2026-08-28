#include "cascade_dump.hpp"
#include "json_writer.hpp"
#include "css_dump.hpp"
#include "../../core/utility.hpp"
#include "../../css/cascade/engine.hpp"
#include "../../html/dom.hpp"

using browser::f32;
// ---------------------------------------------------------------------------
// Cascade dump
// ---------------------------------------------------------------------------
std::string css_value_to_json(const browser::css::CSSValue &v) {
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

std::string dump_cascade_element(const browser::html::Element *el, const browser::css::ComputedStyle &style) {
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

