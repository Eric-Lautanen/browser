#pragma once
#include "../../css/cascade/engine.hpp"
#include "../../html/dom.hpp"
#include <string>
std::string css_value_to_json(const browser::css::CSSValue &v);
std::string dump_cascade_element(const browser::html::Element *el, const browser::css::ComputedStyle &style);
