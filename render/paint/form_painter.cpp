#include "form_painter.hpp"

namespace browser::render {

void paint_form_control(DisplayList& list, const css::LayoutNode* node, float ox, float oy) {
    (void)list; (void)node; (void)ox; (void)oy;
    // TODO: move Painter::paint_node form-control dispatch (painter.cpp:153-453) here
}

} // namespace browser::render
