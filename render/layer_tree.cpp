#include "layer_tree.hpp"

#include "../css/layout.hpp"
#include "paint/painter.hpp"

namespace browser::render {

    bool LayerTreeBuilder::is_scrollable_node(css::LayoutNode *node) {
        auto *overflow = node->style().get("overflow");
        if (overflow && overflow->type == css::CSSValue::Type::KEYWORD) {
            if (overflow->keyword == "scroll" || overflow->keyword == "auto")
                return true;
        }
        auto *ox = node->style().get("overflow-x");
        if (ox && ox->type == css::CSSValue::Type::KEYWORD) {
            if (ox->keyword == "scroll" || ox->keyword == "auto")
                return true;
        }
        auto *oy = node->style().get("overflow-y");
        if (oy && oy->type == css::CSSValue::Type::KEYWORD) {
            if (oy->keyword == "scroll" || oy->keyword == "auto")
                return true;
        }
        return false;
    }

    bool LayerTreeBuilder::needs_own_layer(css::LayoutNode *node) {
        if (is_scrollable_node(node))
            return true;

        auto *pos = node->style().get("position");
        if (pos && pos->type == css::CSSValue::Type::KEYWORD) {
            if (pos->keyword == "fixed")
                return true;
            if (pos->keyword == "sticky")
                return true;
        }

        if (node->has_transform)
            return true;

        auto *opacity_val = node->style().get("opacity");
        if (opacity_val && opacity_val->type == css::CSSValue::Type::NUMBER) {
            if (opacity_val->number < 1.0f)
                return true;
        }

        if (css::LayoutEngine::creates_stacking_context(node->style()))
            return true;

        auto *will_change = node->style().get("will-change");
        if (will_change && will_change->type == css::CSSValue::Type::STRING) {
            if (will_change->string_value.find("transform") != std::string::npos)
                return true;
        }

        return false;
    }

    std::unique_ptr<LayerTree> LayerTreeBuilder::build(css::LayoutNode *layout_root) {
        auto tree = std::make_unique<LayerTree>();
        if (!layout_root)
            return tree;

        auto root_layer = std::make_unique<Layer>();
        root_layer->layer_id = tree->next_layer_id++;
        root_layer->layout_node = layout_root;
        root_layer->bounds = {0, 0, layout_root->content.width, layout_root->content.height};
        root_layer->is_scrollable = false;

        // The root layer is also the viewport scroll layer if body is scrollable
        for (auto &child : layout_root->children) {
            if (child->parent) {
                auto *pos = child->style().get("position");
                bool is_abs = pos && pos->type == css::CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (is_abs)
                    continue;
            }
            auto child_layer = build_layer(child.get(), root_layer.get(), tree, 0, 0);
            if (child_layer) {
                root_layer->children.push_back(std::move(child_layer));
            }
        }

        tree->all_layers.push_back(root_layer.get());
        tree->root_layer = std::move(root_layer);

        return tree;
    }

    std::unique_ptr<Layer> LayerTreeBuilder::build_layer(
        css::LayoutNode *node, Layer *parent_layer, std::unique_ptr<LayerTree> &tree, f32 offset_x, f32 offset_y) {
        if (!node)
            return nullptr;

        bool own_layer = needs_own_layer(node);

        if (own_layer) {
            auto layer = std::make_unique<Layer>();
            layer->layer_id = tree->next_layer_id++;
            layer->parent = parent_layer;
            layer->layout_node = node;
            layer->is_scrollable = is_scrollable_node(node);
            layer->is_fixed = false;
            layer->is_sticky = false;

            auto *pos = node->style().get("position");
            if (pos && pos->type == css::CSSValue::Type::KEYWORD) {
                if (pos->keyword == "fixed")
                    layer->is_fixed = true;
                if (pos->keyword == "sticky")
                    layer->is_sticky = true;
            }

            layer->transform = node->transform_matrix;
            layer->opacity = node->opacity;

            f32 bx = offset_x + node->content.x;
            f32 by = offset_y + node->content.y;
            layer->bounds = {bx, by, node->content.width, node->content.height};

            if (layer->is_scrollable) {
                f32 cbx = offset_x + node->content.x - node->padding.left;
                f32 cby = offset_y + node->content.y - node->padding.top;
                f32 cbw = node->content.width + node->padding.left + node->padding.right;
                f32 cbh = node->content.height + node->padding.top + node->padding.bottom;
                layer->clip_rect = {cbx, cby, cbw, cbh};
            }

            // Process children
            for (auto &child : node->children) {
                auto child_layer = build_layer(child.get(), layer.get(), tree, bx, by);
                if (child_layer) {
                    layer->children.push_back(std::move(child_layer));
                }
            }

            tree->all_layers.push_back(layer.get());
            return layer;  // Ownership transferred to caller
        } else {
            // Not a layer itself — children inherit the parent layer.
            for (auto &child : node->children) {
                auto child_layer = build_layer(child.get(),
                                               parent_layer,
                                               tree,
                                               offset_x + node->content.x + node->scroll_offset_x,
                                               offset_y + node->content.y + node->scroll_offset_y);
                if (child_layer && parent_layer) {
                    parent_layer->children.push_back(std::move(child_layer));
                }
            }
            return nullptr;
        }
    }

}  // namespace browser::render
