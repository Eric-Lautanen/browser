#include "background_painter.hpp"

namespace browser::render {

void paint_background_commands(DisplayList& list,
                               const css::LayoutNode* node,
                               float ox, float oy) {
    (void)list; (void)node; (void)ox; (void)oy;
}

} // namespace browser::render
