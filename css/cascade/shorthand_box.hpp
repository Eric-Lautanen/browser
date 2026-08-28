#pragma once
#include "engine.hpp"
#include <string>

namespace browser::css {

// CS-refactor: Extract four-side shorthand expansion (margin/padding/border-width)
// from cascade/engine.cpp:606-680. Second step will cover inset/border-radius etc.
void expand_four_sides(ComputedStyle& style, const std::string& base, const std::string& value);
void expand_four_sides(ComputedStyle& style, const std::string& base, const CSSValue& val);

} // namespace browser::css
