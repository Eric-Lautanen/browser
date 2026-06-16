#include "../layout.hpp"

#include <algorithm>
#include <vector>

namespace browser::css {

    namespace {

        bool is_row_direction(FlexDirection d) {
            return d == FlexDirection::ROW || d == FlexDirection::ROW_REVERSE;
        }

        bool is_reverse_direction(FlexDirection d) {
            return d == FlexDirection::ROW_REVERSE || d == FlexDirection::COLUMN_REVERSE;
        }

    }  // namespace

    FlexConfig LayoutEngine::resolve_flex_config(const ComputedStyle &style, f32 font_size) const {
        FlexConfig cfg;

        auto *dir = style.get("flex-direction");
        if (dir && dir->type == CSSValue::Type::KEYWORD) {
            if (dir->keyword == "row-reverse")
                cfg.direction = FlexDirection::ROW_REVERSE;
            else if (dir->keyword == "column")
                cfg.direction = FlexDirection::COLUMN;
            else if (dir->keyword == "column-reverse")
                cfg.direction = FlexDirection::COLUMN_REVERSE;
        }

        auto *wrap = style.get("flex-wrap");
        if (wrap && wrap->type == CSSValue::Type::KEYWORD) {
            if (wrap->keyword == "wrap")
                cfg.wrap = FlexWrap::WRAP;
            else if (wrap->keyword == "wrap-reverse")
                cfg.wrap = FlexWrap::WRAP_REVERSE;
        }

        auto *jc = style.get("justify-content");
        if (jc && jc->type == CSSValue::Type::KEYWORD) {
            if (jc->keyword == "flex-end")
                cfg.justify_content = JustifyContent::FLEX_END;
            else if (jc->keyword == "center")
                cfg.justify_content = JustifyContent::CENTER;
            else if (jc->keyword == "space-between")
                cfg.justify_content = JustifyContent::SPACE_BETWEEN;
            else if (jc->keyword == "space-around")
                cfg.justify_content = JustifyContent::SPACE_AROUND;
            else if (jc->keyword == "space-evenly")
                cfg.justify_content = JustifyContent::SPACE_EVENLY;
        }

        auto *ai = style.get("align-items");
        if (ai && ai->type == CSSValue::Type::KEYWORD) {
            if (ai->keyword == "flex-start")
                cfg.align_items = AlignItems::FLEX_START;
            else if (ai->keyword == "flex-end")
                cfg.align_items = AlignItems::FLEX_END;
            else if (ai->keyword == "center")
                cfg.align_items = AlignItems::CENTER;
            else if (ai->keyword == "baseline")
                cfg.align_items = AlignItems::BASELINE;
        }

        auto *ac = style.get("align-content");
        if (ac && ac->type == CSSValue::Type::KEYWORD) {
            if (ac->keyword == "flex-start")
                cfg.align_content = AlignContent::FLEX_START;
            else if (ac->keyword == "flex-end")
                cfg.align_content = AlignContent::FLEX_END;
            else if (ac->keyword == "center")
                cfg.align_content = AlignContent::CENTER;
            else if (ac->keyword == "space-between")
                cfg.align_content = AlignContent::SPACE_BETWEEN;
            else if (ac->keyword == "space-around")
                cfg.align_content = AlignContent::SPACE_AROUND;
        }

        auto *gap = style.get("gap");
        if (gap && gap->type == CSSValue::Type::LENGTH) {
            f32 g = resolve_length(gap->length, 0, font_size);
            cfg.row_gap = g;
            cfg.column_gap = g;
        }

        auto *rg = style.get("row-gap");
        if (rg && rg->type == CSSValue::Type::LENGTH) {
            cfg.row_gap = resolve_length(rg->length, 0, font_size);
        }

        auto *cg = style.get("column-gap");
        if (cg && cg->type == CSSValue::Type::LENGTH) {
            cfg.column_gap = resolve_length(cg->length, 0, font_size);
        }

        return cfg;
    }

    FlexItem LayoutEngine::resolve_flex_item(
        LayoutNode *child, const FlexConfig &config, f32 containing_main, f32 containing_cross, f32 font_size) const {
        FlexItem item;
        item.node = child;

        auto *fg = child->style().get("flex-grow");
        if (fg && fg->type == CSSValue::Type::NUMBER)
            item.flex_grow = fg->number;

        auto *fs = child->style().get("flex-shrink");
        if (fs && fs->type == CSSValue::Type::NUMBER)
            item.flex_shrink = fs->number;

        auto *fb = child->style().get("flex-basis");
        if (fb) {
            if (fb->type == CSSValue::Type::LENGTH) {
                item.flex_basis = resolve_length(fb->length, containing_main, font_size);
            }
        }

        auto *as = child->style().get("align-self");
        if (as && as->type == CSSValue::Type::KEYWORD) {
            if (as->keyword == "stretch")
                item.align_self = AlignSelf::STRETCH;
            else if (as->keyword == "flex-start")
                item.align_self = AlignSelf::FLEX_START;
            else if (as->keyword == "flex-end")
                item.align_self = AlignSelf::FLEX_END;
            else if (as->keyword == "center")
                item.align_self = AlignSelf::CENTER;
            else if (as->keyword == "baseline")
                item.align_self = AlignSelf::BASELINE;
        }

        bool is_row = config.direction == FlexDirection::ROW || config.direction == FlexDirection::ROW_REVERSE;

        EdgeSizes margins;
        margins.left = resolve_side_value(child->style(), "margin-left", "margin", containing_main, font_size);
        margins.right = resolve_side_value(child->style(), "margin-right", "margin", containing_main, font_size);
        margins.top = resolve_side_value(child->style(), "margin-top", "margin", containing_cross, font_size);
        margins.bottom = resolve_side_value(child->style(), "margin-bottom", "margin", containing_cross, font_size);
        child->margin = margins;

        if (is_row) {
            item.main_margin_start = margins.left;
            item.main_margin_end = margins.right;
            item.cross_margin_start = margins.top;
            item.cross_margin_end = margins.bottom;
        } else {
            item.main_margin_start = margins.top;
            item.main_margin_end = margins.bottom;
            item.cross_margin_start = margins.left;
            item.cross_margin_end = margins.right;
        }

        EdgeSizes borders, paddings;
        borders.left =
            resolve_side_value(child->style(), "border-left-width", "border-width", containing_main, font_size);
        borders.right =
            resolve_side_value(child->style(), "border-right-width", "border-width", containing_main, font_size);
        borders.top =
            resolve_side_value(child->style(), "border-top-width", "border-width", containing_cross, font_size);
        borders.bottom =
            resolve_side_value(child->style(), "border-bottom-width", "border-width", containing_cross, font_size);
        paddings.left = resolve_side_value(child->style(), "padding-left", "padding", containing_main, font_size);
        paddings.right = resolve_side_value(child->style(), "padding-right", "padding", containing_main, font_size);
        paddings.top = resolve_side_value(child->style(), "padding-top", "padding", containing_cross, font_size);
        paddings.bottom = resolve_side_value(child->style(), "padding-bottom", "padding", containing_cross, font_size);

        child->border = borders;
        child->padding = paddings;

        f32 bp_main_start, bp_main_end, bp_cross_start, bp_cross_end;
        if (is_row) {
            bp_main_start = borders.left + paddings.left;
            bp_main_end = borders.right + paddings.right;
            bp_cross_start = borders.top + paddings.top;
            bp_cross_end = borders.bottom + paddings.bottom;
        } else {
            bp_main_start = borders.top + paddings.top;
            bp_main_end = borders.bottom + paddings.bottom;
            bp_cross_start = borders.left + paddings.left;
            bp_cross_end = borders.right + paddings.right;
        }

        item.main_border_padding = bp_main_start + bp_main_end;
        item.cross_border_padding = bp_cross_start + bp_cross_end;

        if (item.flex_basis >= 0) {
            item.base_size = item.flex_basis;
        } else {
            auto *size_prop = child->style().get(is_row ? "width" : "height");
            if (size_prop && size_prop->type == CSSValue::Type::LENGTH) {
                item.base_size =
                    resolve_length(size_prop->length, is_row ? containing_main : containing_cross, font_size);
            }
        }

        item.hypothetical_main = item.base_size + item.main_border_padding;
        return item;
    }

    std::vector<FlexLine> LayoutEngine::distribute_lines(std::vector<FlexItem> &items,
                                                         const FlexConfig &config,
                                                         f32 container_main) const {
        std::vector<FlexLine> lines;

        if (config.wrap == FlexWrap::NOWRAP || items.empty()) {
            FlexLine line;
            for (auto &item : items) line.items.push_back(&item);
            lines.push_back(std::move(line));
        } else {
            f32 gap = is_row_direction(config.direction) ? config.column_gap : config.row_gap;
            FlexLine current;
            f32 current_main = 0;

            for (auto &item : items) {
                f32 item_main = item.hypothetical_main;
                if (!current.items.empty() && current_main + item_main > container_main) {
                    lines.push_back(std::move(current));
                    current = FlexLine();
                    current_main = 0;
                }
                current.items.push_back(&item);
                current_main += item_main;
                if (current.items.size() > 1)
                    current_main += gap;
            }

            if (!current.items.empty()) {
                lines.push_back(std::move(current));
            }

            if (config.wrap == FlexWrap::WRAP_REVERSE) {
                std::reverse(lines.begin(), lines.end());
            }
        }

        return lines;
    }

    void LayoutEngine::distribute_free_space(FlexLine &line, f32 container_main, const FlexConfig &config) const {
        if (line.items.empty())
            return;

        f32 gap = is_row_direction(config.direction) ? config.column_gap : config.row_gap;
        f32 total_hypothetical = 0;
        for (auto *item : line.items) {
            total_hypothetical += item->hypothetical_main;
        }
        total_hypothetical += gap * static_cast<f32>(line.items.size() - 1);
        f32 free_space = container_main - total_hypothetical;

        if (free_space > 0) {
            f32 total_grow = 0;
            for (auto *item : line.items) total_grow += item->flex_grow;
            if (total_grow > 0) {
                for (auto *item : line.items) {
                    item->target_main = item->hypothetical_main + free_space * (item->flex_grow / total_grow);
                }
            } else {
                for (auto *item : line.items) item->target_main = item->hypothetical_main;
            }
        } else if (free_space < 0) {
            f32 total_scaled_shrink = 0;
            for (auto *item : line.items) {
                total_scaled_shrink += item->flex_shrink * item->hypothetical_main;
            }
            if (total_scaled_shrink > 0) {
                for (auto *item : line.items) {
                    f32 scaled = item->flex_shrink * item->hypothetical_main;
                    item->target_main = item->hypothetical_main + free_space * (scaled / total_scaled_shrink);
                    if (item->target_main < item->main_border_padding) {
                        item->target_main = item->main_border_padding;
                    }
                }
            } else {
                for (auto *item : line.items) item->target_main = item->hypothetical_main;
            }
        } else {
            for (auto *item : line.items) item->target_main = item->hypothetical_main;
        }

        if (is_reverse_direction(config.direction)) {
            std::reverse(line.items.begin(), line.items.end());
        }
    }

    void LayoutEngine::compute_cross_sizes(FlexLine &line,
                                           f32 container_cross,
                                           const FlexConfig &config,
                                           f32 font_size) {
        if (line.items.empty())
            return;

        bool is_row = is_row_direction(config.direction);

        for (auto *item : line.items) {
            AlignSelf effective_align = item->align_self;
            if (effective_align == AlignSelf::AUTO) {
                effective_align = static_cast<AlignSelf>(static_cast<int>(config.align_items));
            }

            if (effective_align == AlignSelf::STRETCH) {
                auto *cross_size_prop = item->node->style().get(is_row ? "height" : "width");
                if (cross_size_prop && cross_size_prop->type == CSSValue::Type::LENGTH) {
                    item->cross_size = resolve_length(cross_size_prop->length, container_cross, font_size);
                } else {
                    item->cross_size = container_cross - item->cross_margin_start - item->cross_margin_end -
                                       item->cross_border_padding;
                    if (item->cross_size < 0)
                        item->cross_size = 0;
                }
            } else {
                auto *cross_size_prop = item->node->style().get(is_row ? "height" : "width");
                if (cross_size_prop && cross_size_prop->type == CSSValue::Type::LENGTH) {
                    item->cross_size = resolve_length(cross_size_prop->length, container_cross, font_size);
                } else {
                    f32 main_content = item->target_main - item->main_border_padding;
                    if (is_row) {
                        item->node->content.width = main_content;
                        if (is_flex_element(item->node->style())) {
                            layout_flex(item->node, main_content, container_cross);
                        } else {
                            layout_children(item->node, main_content, container_cross);
                        }
                        item->cross_size = item->node->content.height;
                    } else {
                        item->node->content.height = main_content;
                        if (is_flex_element(item->node->style())) {
                            layout_flex(item->node, container_cross, main_content);
                        } else {
                            layout_children(item->node, container_cross, main_content);
                        }
                        item->cross_size = item->node->content.width;
                    }
                }
            }
        }

        f32 max_cross = 0;
        for (auto *item : line.items) {
            f32 total =
                item->cross_size + item->cross_margin_start + item->cross_margin_end + item->cross_border_padding;
            if (total > max_cross)
                max_cross = total;
        }
        line.cross_size = max_cross;
    }

    void LayoutEngine::align_lines(std::vector<FlexLine> &lines, f32 container_cross, const FlexConfig &config) const {
        if (lines.empty())
            return;

        f32 gap = is_row_direction(config.direction) ? config.row_gap : config.column_gap;

        f32 total_cross = 0;
        for (auto &line : lines) {
            total_cross += line.cross_size;
        }
        total_cross += gap * static_cast<f32>(lines.size() - 1);
        f32 free_cross = container_cross - total_cross;

        f32 initial_offset = 0;
        f32 gap_extra = gap;

        switch (config.align_content) {
            case AlignContent::FLEX_START:
                initial_offset = 0;
                gap_extra = gap;
                break;
            case AlignContent::FLEX_END:
                initial_offset = free_cross;
                gap_extra = gap;
                break;
            case AlignContent::CENTER:
                initial_offset = free_cross / 2;
                gap_extra = gap;
                break;
            case AlignContent::SPACE_BETWEEN:
                initial_offset = 0;
                if (lines.size() > 1)
                    gap_extra = free_cross / static_cast<f32>(lines.size() - 1) + gap;
                else
                    gap_extra = 0;
                break;
            case AlignContent::SPACE_AROUND: {
                f32 spacing = free_cross / static_cast<f32>(lines.size() * 2);
                initial_offset = spacing;
                gap_extra = spacing * 2 + gap;
                break;
            }
            case AlignContent::STRETCH: {
                if (lines.size() > 0 && free_cross > 0) {
                    f32 extra = free_cross / static_cast<f32>(lines.size());
                    for (auto &line : lines) line.cross_size += extra;
                }
                initial_offset = 0;
                gap_extra = gap;
                break;
            }
        }

        f32 cross_pos = initial_offset;
        for (auto &line : lines) {
            for (auto *item : line.items) {
                item->cross_offset = cross_pos;
                AlignSelf effective = item->align_self;
                if (effective == AlignSelf::AUTO) {
                    effective = static_cast<AlignSelf>(static_cast<int>(config.align_items));
                }
                if (effective != AlignSelf::STRETCH) {
                    f32 line_cross = line.cross_size;
                    f32 item_cross = item->cross_size + item->cross_border_padding + item->cross_margin_start +
                                     item->cross_margin_end;
                    f32 extra = line_cross - item_cross;
                    if (extra > 0) {
                        if (effective == AlignSelf::CENTER) {
                            item->cross_offset += extra / 2 + item->cross_margin_start;
                        } else if (effective == AlignSelf::FLEX_END) {
                            item->cross_offset += extra + item->cross_margin_start;
                        } else {
                            item->cross_offset += item->cross_margin_start;
                        }
                    } else {
                        item->cross_offset += item->cross_margin_start;
                    }
                } else {
                    item->cross_offset += item->cross_margin_start;
                }
            }
            cross_pos += line.cross_size + gap_extra;
        }
    }

    void LayoutEngine::layout_flex(LayoutNode *node, f32 containing_width, f32 containing_height) {
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

        FlexConfig config = resolve_flex_config(node->style(), font_size);

        bool is_row = is_row_direction(config.direction);

        f32 container_main;
        if (is_row) {
            auto *wv = node->style().get("width");
            if (wv && wv->type == CSSValue::Type::LENGTH) {
                container_main = resolve_length(wv->length, containing_width, font_size);
            } else {
                container_main = node->content.width;
            }
        } else {
            auto *hv = node->style().get("height");
            if (hv && hv->type == CSSValue::Type::LENGTH) {
                container_main = resolve_length(hv->length, containing_height, font_size);
            } else {
                container_main = containing_height;
            }
        }

        f32 container_cross;
        if (is_row) {
            auto *hv = node->style().get("height");
            if (hv && hv->type == CSSValue::Type::LENGTH) {
                container_cross = resolve_length(hv->length, containing_height, font_size);
            } else {
                container_cross = containing_height;
            }
        } else {
            auto *wv = node->style().get("width");
            if (wv && wv->type == CSSValue::Type::LENGTH) {
                container_cross = resolve_length(wv->length, containing_width, font_size);
            } else {
                container_cross = node->content.width;
            }
        }

        std::vector<FlexItem> items;
        for (auto &child : node->children) {
            auto *pos = child->style().get("position");
            bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
            if (is_absolute)
                continue;

            f32 containing_main = container_main;
            f32 containing_cross = container_cross;
            items.push_back(resolve_flex_item(child.get(), config, containing_main, containing_cross, font_size));
        }

        std::stable_sort(items.begin(), items.end(), [](const FlexItem &a, const FlexItem &b) {
            auto *ao = a.node->style().get("order");
            auto *bo = b.node->style().get("order");
            f32 av = (ao && ao->type == CSSValue::Type::NUMBER) ? ao->number : 0;
            f32 bv = (bo && bo->type == CSSValue::Type::NUMBER) ? bo->number : 0;
            return av < bv;
        });

        std::vector<FlexLine> lines = distribute_lines(items, config, container_main);

        for (auto &line : lines) {
            distribute_free_space(line, container_main, config);
        }

        for (auto &line : lines) {
            compute_cross_sizes(line, container_cross, config, font_size);
        }

        align_lines(lines, container_cross, config);

        f32 gap = is_row ? config.column_gap : config.row_gap;

        for (auto &line : lines) {
            f32 total_target = 0;
            for (auto *item : line.items) total_target += item->target_main;
            total_target += gap * static_cast<f32>(line.items.size() - 1);
            f32 free_space = container_main - total_target;

            f32 initial_offset = 0;
            f32 gap_extra = gap;

            switch (config.justify_content) {
                case JustifyContent::FLEX_START:
                    initial_offset = 0;
                    gap_extra = gap;
                    break;
                case JustifyContent::FLEX_END:
                    initial_offset = free_space;
                    gap_extra = gap;
                    break;
                case JustifyContent::CENTER:
                    initial_offset = free_space / 2;
                    gap_extra = gap;
                    break;
                case JustifyContent::SPACE_BETWEEN:
                    initial_offset = 0;
                    if (line.items.size() > 1)
                        gap_extra = free_space / static_cast<f32>(line.items.size() - 1) + gap;
                    else
                        gap_extra = 0;
                    break;
                case JustifyContent::SPACE_AROUND: {
                    f32 spacing = free_space / static_cast<f32>(line.items.size());
                    initial_offset = spacing / 2;
                    gap_extra = spacing + gap;
                    break;
                }
                case JustifyContent::SPACE_EVENLY: {
                    f32 spacing = free_space / static_cast<f32>(line.items.size() + 1);
                    initial_offset = spacing;
                    gap_extra = spacing + gap;
                    break;
                }
            }

            f32 main_pos = initial_offset;
            for (auto *item : line.items) {
                f32 main_start = main_pos;
                f32 main_end = main_start + item->target_main;

                if (is_row) {
                    item->node->content.x =
                        main_start + item->main_margin_start + item->node->border.left + item->node->padding.left;
                    item->node->content.y = item->cross_offset + item->cross_margin_start + item->node->border.top +
                                            item->node->padding.top;
                } else {
                    item->node->content.x = item->cross_offset + item->cross_margin_start + item->node->border.left +
                                            item->node->padding.left;
                    item->node->content.y =
                        main_start + item->main_margin_start + item->node->border.top + item->node->padding.top;
                }

                item->node->content.width = is_row ? item->target_main - item->main_border_padding : item->cross_size;
                item->node->content.height = is_row ? item->cross_size : item->target_main - item->main_border_padding;

                main_pos = main_end + gap_extra;
            }
        }

        for (auto &item : items) {
            if (item.node->children.empty())
                continue;
            f32 cw = item.node->content.width;
            f32 ch = item.node->content.height;
            if (is_flex_element(item.node->style())) {
                f32 saved_x = item.node->content.x;
                f32 saved_y = item.node->content.y;
                layout_flex(item.node, cw, ch);
                item.node->content.x = saved_x;
                item.node->content.y = saved_y;
                item.node->content.width = cw;
                item.node->content.height = ch;
            } else {
                layout_children(item.node, cw, ch);
            }
        }

        if (is_row) {
            auto *hv = node->style().get("height");
            if (!hv || (hv->type == CSSValue::Type::KEYWORD && hv->keyword == "auto")) {
                f32 max_y = 0;
                for (auto &line : lines) {
                    for (auto *item : line.items) {
                        f32 bottom = item->node->content.y + item->node->content.height + item->node->padding.bottom +
                                     item->node->border.bottom + item->node->margin.bottom;
                        if (bottom > max_y)
                            max_y = bottom;
                    }
                }
                node->content.height = max_y;
            }
        } else {
            auto *wv = node->style().get("width");
            if (!wv || (wv->type == CSSValue::Type::KEYWORD && wv->keyword == "auto")) {
                f32 max_x = 0;
                for (auto &line : lines) {
                    for (auto *item : line.items) {
                        f32 right = item->node->content.x + item->node->content.width + item->node->padding.right +
                                    item->node->border.right + item->node->margin.right;
                        if (right > max_x)
                            max_x = right;
                    }
                }
                node->content.width = max_x;
            }
        }
    }

}  // namespace browser::css
