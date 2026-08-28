#pragma once
#include "engine.hpp"
#include <string>

namespace browser::css {

void expand_border(ComputedStyle& style, const CSSValue& val, const std::string& prop);

} // namespace browser::css
