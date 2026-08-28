#pragma once
#include "../../css/layout/types.hpp"
#include "commands.hpp"

namespace browser::render {

// R-Q1: Extract text decoration from Painter::paint_text (painter.cpp:790)
// First step: module boundary for underline/overline/line-through
void paint_text_decoration(DisplayList& list, const css::LayoutNode* node, float ox, float oy);

} // namespace browser::render
