#pragma once
#include "css_values.hpp"
#include "../html/dom.hpp"

namespace browser::css {

bool matches_compound(const std::vector<SimpleSelector>& compound, const html::Element* el);
bool matches_selector(const Selector& sel, const html::Element* el, const html::Node* root);

}
