#include "background_painter.hpp"
#include "../../css/layout.hpp"

namespace browser::render {

void paint_background_commands(DisplayList& list,
                               const css::LayoutNode* node,
                               float ox, float oy,
                               const std::unordered_map<std::string, std::shared_ptr<image::Image>>* images) {
    (void)list; (void)node; (void)ox; (void)oy; (void)images;
    // TODO: move full Painter::paint_background body here (next commit)
}

} // namespace browser::render
