#include "../layout.hpp"

#include <algorithm>
#include <cstdlib>

namespace browser::css {

    void LayoutEngine::layout_block(LayoutNode *node, f32 containing_width, f32 containing_height) {
        if (!node)
            return;

        f32 parent_font_size = root_font_size_;
        if (node->parent) {
            parent_font_size = resolve_font_size(node->parent->style(), root_font_size_);
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
        } else if (wv->type == CSSValue::Type::FUNCTION || wv->type == CSSValue::Type::STRING) {
            width = resolve_func_length(node->style(), wv, containing_width, font_size);
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
            node->content.width = containing_width - h_margin - h_padding - h_border;
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
        if ((ml_auto || mr_auto) && (has_fixed_width || had_maxw)) {
            f32 used_w = node->content.width + h_padding + h_border;
            f32 remaining = containing_width - used_w;
            if (ml_auto && mr_auto && remaining > 0) {
                f32 half = remaining / 2.0f;
                margins.left = half;
                margins.right = half;
            } else if (ml_auto && !mr_auto && remaining > 0) {
                margins.left = remaining;
            } else if (!ml_auto && mr_auto && remaining > 0) {
                margins.right = remaining;
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

        // Intrinsic sizing for form controls
        {
            html::Node *n = node->node();
            if (n && n->type == html::NodeType::ELEMENT) {
                auto *el = static_cast<html::Element *>(n);
                std::string tag = el->tag_name;
                std::string type = el->get_attribute("type");

                if (tag == "input" && (type.empty() || type == "text")) {
                    if (width_auto) {
                        std::string size_attr = el->get_attribute("size");
                        int size = 20;
                        if (!size_attr.empty()) {
                            char *end = nullptr;
                            long s = std::strtol(size_attr.c_str(), &end, 10);
                            if (end != size_attr.c_str() && s > 0)
                                size = static_cast<int>(s);
                        }
                        node->content.width = static_cast<f32>(size) * 8.0f;
                    }
                    if (node->content.height == 0)
                        node->content.height = 20.0f;
                } else if (tag == "input" && type == "checkbox") {
                    node->content.width = 13.0f;
                    node->content.height = 13.0f;
                } else if (tag == "input" && type == "radio") {
                    node->content.width = 13.0f;
                    node->content.height = 13.0f;
                } else if (tag == "button" || (tag == "input" && type == "submit")) {
                    if (width_auto) {
                        std::string label = el->get_attribute("value");
                        if (label.empty())
                            label = tag;
                        node->content.width = static_cast<f32>(label.size()) * 7.0f + 20.0f;
                    }
                    if (node->content.height == 0)
                        node->content.height = font_size + 8.0f;
                } else if (tag == "select") {
                    if (width_auto) {
                        std::string val = el->get_attribute("value");
                        f32 tw = static_cast<f32>(val.size()) * 7.0f + 30.0f;
                        node->content.width = std::max(tw, 50.0f);
                    }
                    if (node->content.height == 0)
                        node->content.height = 20.0f;
                } else if (tag == "textarea") {
                    if (width_auto) {
                        std::string cols_attr = el->get_attribute("cols");
                        int cols = 20;
                        if (!cols_attr.empty()) {
                            char *end = nullptr;
                            long c = std::strtol(cols_attr.c_str(), &end, 10);
                            if (end != cols_attr.c_str() && c > 0)
                                cols = static_cast<int>(c);
                        }
                        node->content.width = static_cast<f32>(cols) * 8.0f;
                    }
                    if (node->content.height == 0) {
                        std::string rows_attr = el->get_attribute("rows");
                        int rows = 2;
                        if (!rows_attr.empty()) {
                            char *end = nullptr;
                            long r = std::strtol(rows_attr.c_str(), &end, 10);
                            if (end != rows_attr.c_str() && r > 0)
                                rows = static_cast<int>(r);
                        }
                        node->content.height = static_cast<f32>(rows) * font_size;
                    }
                }
            }
        }

        // Handle list-item display: generate marker content
        auto *display_val = node->style().get("display");
        bool is_list_item =
            display_val && display_val->type == CSSValue::Type::KEYWORD && display_val->keyword == "list-item";
        if (is_list_item) {
            // Reserve space for the marker on the left
            f32 marker_width = font_size * 1.5f;
            node->content.width = (node->content.width > marker_width) ? node->content.width - marker_width : 0;
            std::string list_style = "disc";
            auto *ls = node->style().get("list-style-type");
            if (ls && ls->type == CSSValue::Type::KEYWORD) {
                list_style = ls->keyword;
            }
            // Build the marker glyph text
            std::string marker_text;
            if (list_style == "disc")
                marker_text = "\xE2\x80\xA2";  // •
            else if (list_style == "circle")
                marker_text = "\xE2\x97\x8B";  // ○
            else if (list_style == "square")
                marker_text = "\xE2\x96\xAA";  // ▪
            else if (list_style == "decimal") {
                int counter = 0;
                html::Node *n = node->node();
                if (n && n->type == html::NodeType::ELEMENT && n->parent) {
                    for (auto &sibling : n->parent->children) {
                        if (sibling.get() == n) {
                            counter++;
                            break;
                        }
                        if (sibling->type == html::NodeType::ELEMENT) {
                            auto *sib_el = static_cast<html::Element *>(sibling.get());
                            if (sib_el->tag_name == "li")
                                counter++;
                        }
                    }
                }
                marker_text = std::to_string(std::max(1, counter)) + ".";
            }

            if (!marker_text.empty()) {
                // Remove previously inserted marker children to prevent duplicates on re-layout
                auto it = node->children.begin();
                while (it != node->children.end()) {
                    if (!(*it)->node()) {
                        it = node->children.erase(it);
                    } else {
                        ++it;
                    }
                }
                auto marker_child = std::make_unique<LayoutNode>(marker_text, node->style());
                marker_child->content.x = -node->padding.left;
                marker_child->content.y = 0;
                marker_child->content.width = marker_width;
                marker_child->content.height = font_size;
                // Insert before other children so it paints behind content
                node->children.insert(node->children.begin(), std::move(marker_child));
            }
            // The marker will be rendered in the padding area
            node->padding.left += marker_width;
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
