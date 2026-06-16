#include "../layout.hpp"

#include <algorithm>

namespace browser::css {

    void LayoutEngine::layout_block(LayoutNode *node, f32 containing_width, f32 containing_height) {
        if (!node)
            return;

        f32 parent_font_size = root_font_size_;
        if (node->parent) {
            auto *pfs = node->parent->style().get("font-size");
            if (pfs && pfs->type == CSSValue::Type::LENGTH && pfs->length.unit == Length::Unit::PX) {
                parent_font_size = pfs->length.value;
            }
        }
        f32 font_size = resolve_font_size(node->style(), parent_font_size);

        bool border_box = false;
        auto *bs = node->style().get("box-sizing");
        if (bs && bs->type == CSSValue::Type::KEYWORD && bs->keyword == "border-box") {
            border_box = true;
        }

        bool width_auto = false;
        f32 width = 0;
        auto *wv = node->style().get("width");
        if (!wv || (wv->type == CSSValue::Type::KEYWORD && wv->keyword == "auto")) {
            width_auto = true;
        } else if (wv->type == CSSValue::Type::LENGTH) {
            width = resolve_length(wv->length, containing_width, font_size);
        }

        EdgeSizes margins;
        margins.top = resolve_side_value(node->style(), "margin-top", "margin", containing_width, font_size);
        margins.bottom = resolve_side_value(node->style(), "margin-bottom", "margin", containing_width, font_size);
        margins.left = resolve_side_value(node->style(), "margin-left", "margin", containing_width, font_size);
        margins.right = resolve_side_value(node->style(), "margin-right", "margin", containing_width, font_size);

        EdgeSizes paddings;
        paddings.top = resolve_side_value(node->style(), "padding-top", "padding", containing_width, font_size);
        paddings.bottom = resolve_side_value(node->style(), "padding-bottom", "padding", containing_width, font_size);
        paddings.left = resolve_side_value(node->style(), "padding-left", "padding", containing_width, font_size);
        paddings.right = resolve_side_value(node->style(), "padding-right", "padding", containing_width, font_size);

        EdgeSizes borders;
        borders.top =
            resolve_side_value(node->style(), "border-top-width", "border-width", containing_width, font_size);
        borders.bottom =
            resolve_side_value(node->style(), "border-bottom-width", "border-width", containing_width, font_size);
        borders.left =
            resolve_side_value(node->style(), "border-left-width", "border-width", containing_width, font_size);
        borders.right =
            resolve_side_value(node->style(), "border-right-width", "border-width", containing_width, font_size);

        if (borders.top == 0) {
            auto *bv = node->style().get("border");
            if (bv && bv->type == CSSValue::Type::LENGTH) {
                f32 bw = resolve_length(bv->length, containing_width, font_size);
                borders = {bw, bw, bw, bw};
            }
        }

        node->margin = margins;
        node->padding = paddings;
        node->border = borders;

        f32 h_padding = paddings.left + paddings.right;
        f32 h_border = borders.left + borders.right;
        f32 h_margin = margins.left + margins.right;

        if (width_auto) {
            if (border_box) {
                node->content.width = containing_width - h_margin;
            } else {
                node->content.width = containing_width - h_margin - h_padding - h_border;
            }
            if (node->content.width < 0)
                node->content.width = 0;
        } else {
            if (border_box) {
                node->content.width = width - h_padding - h_border;
                if (node->content.width < 0)
                    node->content.width = 0;
            } else {
                node->content.width = width;
            }
        }

        auto *maxw = node->style().get("max-width");
        bool had_maxw = false;
        if (maxw && maxw->type == CSSValue::Type::LENGTH) {
            f32 mw = resolve_length(maxw->length, containing_width, font_size);
            if (node->content.width > mw) {
                node->content.width = mw;
                had_maxw = true;
            }
        }
        auto *minw = node->style().get("min-width");
        if (minw && minw->type == CSSValue::Type::LENGTH) {
            f32 mw = resolve_length(minw->length, containing_width, font_size);
            if (node->content.width < mw)
                node->content.width = mw;
        }

        auto *ml = node->style().get("margin-left");
        auto *mr = node->style().get("margin-right");
        bool ml_auto = !ml || (ml->type == CSSValue::Type::KEYWORD && ml->keyword == "auto");
        bool mr_auto = !mr || (mr->type == CSSValue::Type::KEYWORD && mr->keyword == "auto");
        bool has_fixed_width = wv && wv->type == CSSValue::Type::LENGTH;
        if (ml_auto && mr_auto && (has_fixed_width || had_maxw)) {
            f32 used_w = node->content.width + h_padding + h_border;
            f32 remaining = containing_width - used_w;
            if (remaining > 0) {
                f32 half = remaining / 2.0f;
                margins.left = half;
                margins.right = half;
            }
        }
        node->margin = margins;

        if (is_grid_element(node->style())) {
            layout_grid(node, containing_width, containing_height);
            auto *maxh = node->style().get("max-height");
            if (maxh && maxh->type == CSSValue::Type::LENGTH) {
                f32 mh = resolve_length(maxh->length, containing_height, font_size);
                if (node->content.height > mh)
                    node->content.height = mh;
            }
            auto *minh = node->style().get("min-height");
            if (minh && minh->type == CSSValue::Type::LENGTH) {
                f32 mh = resolve_length(minh->length, containing_height, font_size);
                if (node->content.height < mh)
                    node->content.height = mh;
            }
            return;
        }

        if (is_table_element(node->style())) {
            layout_table(node, containing_width, containing_height);
            return;
        }

        if (is_flex_element(node->style())) {
            layout_flex(node, containing_width, containing_height);
            auto *maxh = node->style().get("max-height");
            if (maxh && maxh->type == CSSValue::Type::LENGTH) {
                f32 mh = resolve_length(maxh->length, containing_height, font_size);
                if (node->content.height > mh)
                    node->content.height = mh;
            }
            auto *minh = node->style().get("min-height");
            if (minh && minh->type == CSSValue::Type::LENGTH) {
                f32 mh = resolve_length(minh->length, containing_height, font_size);
                if (node->content.height < mh)
                    node->content.height = mh;
            }
            return;
        }

        auto *float_val = node->style().get("float");
        bool is_floating = float_val && float_val->type == CSSValue::Type::KEYWORD &&
                           (float_val->keyword == "left" || float_val->keyword == "right");
        if (is_floating) {
            node->is_floating = true;
            node->float_direction = float_val->keyword == "left" ? 0 : 1;
        }

        auto *overflow = node->style().get("overflow");
        if (overflow && overflow->type == CSSValue::Type::KEYWORD) {
            if (overflow->keyword == "scroll" || overflow->keyword == "auto") {
                node->is_scrollable = true;
            }
        }

        layout_children(node, node->content.width, containing_height);

        auto *hv = node->style().get("height");
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            node->content.height = resolve_length(hv->length, containing_height, font_size);
        } else {
            f32 max_y = 0;
            for (auto &child : node->children) {
                auto *pos = child->style().get("position");
                bool abs_pos = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (abs_pos)
                    continue;
                f32 child_bottom = child->content.y + child->content.height + child->padding.bottom +
                                   child->border.bottom + child->margin.bottom;
                if (child_bottom > max_y)
                    max_y = child_bottom;
            }
            node->content.height = max_y;
        }

        auto *maxh = node->style().get("max-height");
        if (maxh && maxh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(maxh->length, containing_height, font_size);
            if (node->content.height > mh)
                node->content.height = mh;
        }
        auto *minh = node->style().get("min-height");
        if (minh && minh->type == CSSValue::Type::LENGTH) {
            f32 mh = resolve_length(minh->length, containing_height, font_size);
            if (node->content.height < mh)
                node->content.height = mh;
        }

        apply_transform_to_node(node);
    }

}  // namespace browser::css
