#pragma once
#include "../css/layout/types.hpp"
#include "dom.hpp"

namespace browser::html {

    struct HitTestResult {
        Element *element = nullptr;
        const css::LayoutNode *layout_node = nullptr;
        css::Rect bounding_box;
        css::Rect hit_rect;
        f32 z_index = 0;
        bool is_scrollable = false;
    };

    HitTestResult hit_test(const css::LayoutNode *layout_root, f32 px, f32 py);

}  // namespace browser::html
