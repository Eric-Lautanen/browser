#pragma once
#include <vector>
#include "../../css/layout.hpp"
#include "../renderer.hpp"

namespace browser::render {

void generate_linear_gradient_colors(const css::CSSGradient& grad, f32 w, f32 h,
                                      std::vector<Color>& pixels);

}
