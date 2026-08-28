#include "text_decoration.hpp"

namespace browser::render {

void paint_text_decoration(DisplayList& list, const css::LayoutNode* node, float ox, float oy) {
    (void)list; (void)node; (void)ox; (void)oy;
    // TODO: move full text decoration logic from painter.cpp:790 (line, color, thickness, style)
}

} // namespace browser::render
