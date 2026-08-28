#pragma once
#include "../../css/layout/types.hpp"
#include "commands.hpp"

namespace browser::render {

// R-Q1: Extract form control painting from Painter::paint_node (painter.cpp:153)
// First step: module boundary for input/button/checkbox/select rendering
void paint_form_control(DisplayList& list, const css::LayoutNode* node, float ox, float oy);

} // namespace browser::render
