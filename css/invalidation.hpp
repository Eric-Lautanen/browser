#pragma once
#include "css_values.hpp"

#include <string>

namespace browser::css {

    // CS-P1: how a style change affects the pipeline. Layout changes need the
    // full build-layout-tree + layout walk; paint-only changes are visible
    // after re-running just the painter against the existing layout tree.
    enum class StyleImpact { Layout, PaintOnly };

    // Conservative classifier: anything not proven paint-only is Layout.
    StyleImpact style_change_impact(const std::string &property);

    // Cheap value equality used to skip frames where an animation produced
    // the same value it already applied (finished forwards-fill animations,
    // slow timing functions between ticks).
    bool css_values_equal(const CSSValue &a, const CSSValue &b);

}  // namespace browser::css
