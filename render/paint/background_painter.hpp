#pragma once
#include "commands.hpp"
#include "../../css/layout/types.hpp"
#include <string>
#include <unordered_map>
#include <memory>

namespace browser::css { struct LayoutNode; }
namespace browser::image { struct Image; }

namespace browser::render {

// R-Q1: Extract background painting from Painter::paint_background (painter.cpp:649)
// First step delegates to helper; full body moves next batch
void paint_background_commands(DisplayList& list,
                               const css::LayoutNode* node,
                               float ox, float oy,
                               const std::unordered_map<std::string, std::shared_ptr<image::Image>>* images);

} // namespace browser::render
