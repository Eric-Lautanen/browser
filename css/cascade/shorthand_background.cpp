#include "shorthand_background.hpp"

namespace browser::css {

void expand_background(ComputedStyle& style, const CSSValue& val) {
    // TODO: move full background shorthand from engine.cpp:722 (color/image/repeat/attachment/position/size/origin/clip)
    (void)style; (void)val;
}

} // namespace browser::css
