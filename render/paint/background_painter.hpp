#pragma once
#include "commands.hpp"
#include <string>

namespace browser::css { struct LayoutNode; }

namespace browser::render {

// R-Q1: Extract background painting from Painter::paint_background (painter.cpp:649)
// Stub module boundary — full logic migration in next commit.
void paint_background_commands(DisplayList& list,
                               const css::LayoutNode* node,
                               float ox, float oy);

} // namespace browser::render
