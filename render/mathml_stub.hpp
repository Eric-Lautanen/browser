#pragma once
#include "../css/layout.hpp"
#include "../html/dom.hpp"
#include "paint/commands.hpp"

namespace browser::render {

class MathMLRenderer {
public:
    void render(const html::Element* math_element, DisplayList& commands, f32 font_size = 16);
};

} // namespace browser::render
