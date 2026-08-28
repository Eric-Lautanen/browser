#pragma once
#include "engine.hpp"
#include <string>

namespace browser::css {

// CS-refactor: flex shorthand extracted from cascade/engine.cpp:717
void expand_flex(ComputedStyle& style, const CSSValue& val);

} // namespace browser::css
