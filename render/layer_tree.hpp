#pragma once
#include "../css/layout.hpp"
#include "paint/commands.hpp"

#include <memory>
#include <vector>

namespace browser::render {

    struct Layer {
        u32 layer_id = 0;
        css::Rect bounds;
        f32 scroll_offset_x = 0, scroll_offset_y = 0;
        css::Mat3x3 transform;
        f32 opacity = 1.0f;
        css::Rect clip_rect;
        std::shared_ptr<DisplayList> display_list;
        bool is_scrollable = false;
        bool is_fixed = false;
        bool is_sticky = false;
        i32 stacking_context_index = 0;
        Layer *parent = nullptr;
        std::vector<std::unique_ptr<Layer>> children;
        css::LayoutNode *layout_node = nullptr;

        u32 tile_grid_cols = 0;
        u32 tile_grid_rows = 0;
    };

    class LayerTree {
    public:
        std::unique_ptr<Layer> root_layer;
        u32 next_layer_id = 1;
        std::vector<Layer *> all_layers;
    };

    class LayerTreeBuilder {
    public:
        static std::unique_ptr<LayerTree> build(css::LayoutNode *layout_root);

    private:
        static std::unique_ptr<Layer> build_layer(
            css::LayoutNode *node, Layer *parent_layer, std::unique_ptr<LayerTree> &tree, f32 offset_x, f32 offset_y);
        static bool needs_own_layer(css::LayoutNode *node);
        static bool is_scrollable_node(css::LayoutNode *node);
    };

}  // namespace browser::render
