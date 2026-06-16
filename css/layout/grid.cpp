#include "../grid.hpp"

#include "../layout.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace browser::css {

    namespace {

        std::string get_grid_prop_raw(const ComputedStyle &style, const std::string &prop) {
            auto *v = style.get(prop);
            if (!v)
                return "";
            if (v->type == CSSValue::Type::STRING)
                return v->string_value;
            if (v->type == CSSValue::Type::KEYWORD)
                return v->keyword;
            if (v->type == CSSValue::Type::FUNCTION) {
                if (!v->string_value.empty())
                    return v->string_value;
                return v->keyword;
            }
            if (v->type == CSSValue::Type::NUMBER) {
                return std::to_string(static_cast<i32>(v->number));
            }
            if (v->type == CSSValue::Type::LENGTH) {
                std::string s = std::to_string(v->length.value);
                auto dot = s.find('.');
                if (dot != std::string::npos) {
                    auto last = s.find_last_not_of('0');
                    if (last > dot)
                        s = s.substr(0, last + 1);
                    else if (last == dot)
                        s = s.substr(0, dot);
                }
                switch (v->length.unit) {
                    case Length::Unit::PX:
                        s += "px";
                        break;
                    case Length::Unit::EM:
                        s += "em";
                        break;
                    case Length::Unit::REM:
                        s += "rem";
                        break;
                    case Length::Unit::PERCENT:
                        s += "%";
                        break;
                    default:
                        break;
                }
                return s;
            }
            return "";
        }

        struct GridState {
            std::vector<GridTrackDef> columns, rows;
            struct Cell {
                LayoutNode *item = nullptr;
                i32 col_span = 1, row_span = 1;
            };
            std::map<std::pair<i32, i32>, Cell> cells;
            i32 num_columns = 0, num_rows = 0;
            f32 column_gap = 0, row_gap = 0;
        };

    }  // namespace

    void LayoutEngine::resolve_grid_tracks(std::vector<GridTrackDef> &tracks,
                                           f32 container_size,
                                           f32 font_size,
                                           f32 gap) {
        if (tracks.empty())
            return;

        f32 total_fixed = 0;
        f32 total_fr = 0;
        f32 total_gap = gap * static_cast<f32>(static_cast<i32>(tracks.size()) - 1);

        for (auto &t : tracks) {
            switch (t.type) {
                case GridTrackType::FIXED:
                    t.resolved_size = resolve_length(t.min_size, container_size, font_size);
                    total_fixed += t.resolved_size;
                    break;
                case GridTrackType::MINMAX: {
                    f32 min_val = resolve_length(t.min_size, container_size, font_size);
                    if (t.max_size.unit == Length::Unit::NONE) {
                        t.resolved_size = min_val;
                        total_fixed += t.resolved_size;
                    } else {
                        f32 max_val = resolve_length(t.max_size, container_size, font_size);
                        t.resolved_size = std::max(min_val, max_val);
                        total_fixed += t.resolved_size;
                    }
                    break;
                }
                case GridTrackType::FIT_CONTENT: {
                    f32 max_val = resolve_length(t.max_size, container_size, font_size);
                    t.resolved_size = max_val;
                    total_fixed += t.resolved_size;
                    break;
                }
                case GridTrackType::FLEX:
                    t.resolved_size = 0;
                    total_fr += t.min_size.value;
                    break;
                case GridTrackType::AUTO:
                    t.resolved_size = 0;
                    break;
            }
        }

        if (total_fr > 0) {
            f32 remaining = container_size - total_fixed - total_gap;
            if (remaining < 0)
                remaining = 0;
            for (auto &t : tracks) {
                if (t.type == GridTrackType::FLEX) {
                    t.resolved_size = remaining * (t.min_size.value / total_fr);
                }
            }
        }
    }

    void LayoutEngine::layout_grid(LayoutNode *node, f32 containing_width, f32 containing_height) {
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

        EdgeSizes margins;
        margins.top = resolve_side_value(node->style(), "margin-top", "margin", containing_width, font_size);
        margins.bottom = resolve_side_value(node->style(), "margin-bottom", "margin", containing_width, font_size);
        margins.left = resolve_side_value(node->style(), "margin-left", "margin", containing_width, font_size);
        margins.right = resolve_side_value(node->style(), "margin-right", "margin", containing_width, font_size);
        node->margin = margins;

        EdgeSizes paddings;
        paddings.top = resolve_side_value(node->style(), "padding-top", "padding", containing_width, font_size);
        paddings.bottom = resolve_side_value(node->style(), "padding-bottom", "padding", containing_width, font_size);
        paddings.left = resolve_side_value(node->style(), "padding-left", "padding", containing_width, font_size);
        paddings.right = resolve_side_value(node->style(), "padding-right", "padding", containing_width, font_size);
        node->padding = paddings;

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
        node->border = borders;

        f32 h_padding = paddings.left + paddings.right;
        f32 h_border = borders.left + borders.right;
        f32 h_margin = margins.left + margins.right;

        f32 container_width = containing_width - h_margin - h_padding - h_border;
        if (container_width < 0)
            container_width = 0;

        auto *wv = node->style().get("width");
        if (wv && wv->type == CSSValue::Type::LENGTH) {
            container_width = resolve_length(wv->length, containing_width, font_size);
        }

        f32 container_height = containing_height;
        auto *hv = node->style().get("height");
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            container_height = resolve_length(hv->length, containing_height, font_size);
        }

        node->content.width = container_width;

        std::string cols_str = get_grid_prop_raw(node->style(), "grid-template-columns");
        std::string rows_str = get_grid_prop_raw(node->style(), "grid-template-rows");

        GridState state;
        state.columns = parse_track_list(cols_str, container_width, font_size);
        state.rows = parse_track_list(rows_str, container_height, font_size);

        std::unordered_map<std::string, std::pair<i32, i32>> named_areas;
        std::vector<std::vector<std::string>> area_grid;
        i32 area_num_cols = 0, area_num_rows = 0;
        {
            std::string areas_str = get_grid_prop_raw(node->style(), "grid-template-areas");
            if (!areas_str.empty()) {
                std::vector<std::string> area_tokens;
                std::string cur;
                for (char c : areas_str) {
                    if (c == ' ' || c == '\t') {
                        if (!cur.empty()) {
                            area_tokens.push_back(cur);
                            cur.clear();
                        }
                    } else
                        cur += c;
                }
                if (!cur.empty())
                    area_tokens.push_back(cur);

                area_num_cols = static_cast<i32>(state.columns.size());
                if (area_num_cols == 0 && !area_tokens.empty()) {
                    area_num_cols = 1;
                }
                if (area_num_cols > 0 && !area_tokens.empty()) {
                    area_num_rows = static_cast<i32>(area_tokens.size()) / area_num_cols;
                    if (area_num_rows > 0 && static_cast<i32>(area_tokens.size()) % area_num_cols == 0) {
                        for (i32 r = 0; r < area_num_rows; r++) {
                            std::vector<std::string> row;
                            for (i32 c = 0; c < area_num_cols; c++) {
                                row.push_back(area_tokens[static_cast<size_t>(r * area_num_cols + c)]);
                            }
                            area_grid.push_back(row);
                        }
                        for (i32 r = 0; r < area_num_rows; r++) {
                            for (i32 c = 0; c < area_num_cols; c++) {
                                const std::string &name = area_grid[static_cast<size_t>(r)][static_cast<size_t>(c)];
                                if (name != "." && named_areas.find(name) == named_areas.end()) {
                                    named_areas[name] = {c, r};
                                }
                            }
                        }
                    }
                }
                if (area_num_rows > 0 && state.rows.size() < static_cast<size_t>(area_num_rows)) {
                    while (state.rows.size() < static_cast<size_t>(area_num_rows)) {
                        state.rows.push_back(GridTrackDef{
                            GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                    }
                    state.num_rows = static_cast<i32>(state.rows.size());
                }
            }
        }

        if (state.columns.empty()) {
            state.columns.push_back(
                GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
        }

        auto *gap_v = node->style().get("gap");
        if (gap_v && gap_v->type == CSSValue::Type::LENGTH) {
            f32 g = resolve_length(gap_v->length, container_width, font_size);
            state.column_gap = g;
            state.row_gap = g;
        }
        auto *cg = node->style().get("column-gap");
        if (cg && cg->type == CSSValue::Type::LENGTH) {
            state.column_gap = resolve_length(cg->length, container_width, font_size);
        }
        auto *rg = node->style().get("row-gap");
        if (rg && rg->type == CSSValue::Type::LENGTH) {
            state.row_gap = resolve_length(rg->length, container_width, font_size);
        }

        state.num_columns = static_cast<i32>(state.columns.size());
        state.num_rows = static_cast<i32>(state.rows.size());

        std::vector<LayoutNode *> unplaced;
        for (auto &child : node->children) {
            auto *pos = child->style().get("position");
            bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
            if (is_absolute)
                continue;

            std::string gc_str = get_grid_prop_raw(child->style(), "grid-column");
            std::string gr_str = get_grid_prop_raw(child->style(), "grid-row");
            std::string ga_str = get_grid_prop_raw(child->style(), "grid-area");

            GridPlacement col_place = gc_str.empty() ? GridPlacement{} : parse_grid_line(gc_str);
            GridPlacement row_place = gr_str.empty() ? GridPlacement{} : parse_grid_line(gr_str);

            if (!ga_str.empty() && gc_str.empty() && gr_str.empty()) {
                auto it = named_areas.find(ga_str);
                if (it != named_areas.end()) {
                    col_place.line_start = it->second.first + 1;
                    row_place.line_start = it->second.second + 1;
                    col_place.is_explicit = true;
                    row_place.is_explicit = true;
                }
            }

            bool has_explicit = (col_place.line_start > 0 || row_place.line_start > 0);

            if (has_explicit) {
                i32 col = col_place.line_start > 0 ? col_place.line_start - 1 : 0;
                i32 row = row_place.line_start > 0 ? row_place.line_start - 1 : 0;
                i32 col_span = static_cast<i32>(col_place.span);
                i32 row_span = static_cast<i32>(row_place.span);

                while (col + col_span > state.num_columns) {
                    state.columns.push_back(
                        GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                    state.num_columns = static_cast<i32>(state.columns.size());
                }
                while (row + row_span > state.num_rows) {
                    state.rows.push_back(
                        GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                    state.num_rows = static_cast<i32>(state.rows.size());
                }

                for (i32 c = 0; c < col_span; c++) {
                    for (i32 r = 0; r < row_span; r++) {
                        state.cells[{col + c, row + r}] = GridState::Cell{child.get(), col_span, row_span};
                    }
                }
            } else {
                unplaced.push_back(child.get());
            }
        }

        if (!unplaced.empty()) {
            i32 cursor_col = 0;
            i32 cursor_row = 0;
            for (auto *item : unplaced) {
                std::string gc_str = get_grid_prop_raw(item->style(), "grid-column");
                std::string gr_str = get_grid_prop_raw(item->style(), "grid-row");
                GridPlacement cp = gc_str.empty() ? GridPlacement{} : parse_grid_line(gc_str);
                GridPlacement rp = gr_str.empty() ? GridPlacement{} : parse_grid_line(gr_str);

                i32 col_span = static_cast<i32>(cp.span);
                i32 row_span = static_cast<i32>(rp.span);

                while (cursor_col + col_span > state.num_columns) {
                    state.columns.push_back(
                        GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                    state.num_columns = static_cast<i32>(state.columns.size());
                }

                bool placed = false;
                i32 safety = 0;
                while (!placed && safety < 10000) {
                    safety++;
                    bool fits = true;
                    for (i32 dc = 0; dc < col_span && fits; dc++) {
                        for (i32 dr = 0; dr < row_span && fits; dr++) {
                            i32 check_col = cursor_col + dc;
                            i32 check_row = cursor_row + dr;
                            if (check_col >= state.num_columns) {
                                fits = false;
                            }
                            auto it = state.cells.find({check_col, check_row});
                            if (it != state.cells.end()) {
                                fits = false;
                            }
                        }
                    }
                    if (fits) {
                        for (i32 dc = 0; dc < col_span; dc++) {
                            for (i32 dr = 0; dr < row_span; dr++) {
                                state.cells[{cursor_col + dc, cursor_row + dr}] =
                                    GridState::Cell{item, col_span, row_span};
                            }
                        }
                        placed = true;
                    } else {
                        cursor_col++;
                        if (cursor_col + col_span > state.num_columns) {
                            cursor_col = 0;
                            cursor_row++;
                        }
                    }
                }
                while (cursor_row + row_span > state.num_rows) {
                    state.rows.push_back(
                        GridTrackDef{GridTrackType::AUTO, Length{0, Length::Unit::PX}, Length{0, Length::Unit::PX}});
                    state.num_rows = static_cast<i32>(state.rows.size());
                }
                cursor_col += col_span;
                if (cursor_col >= state.num_columns) {
                    cursor_col = 0;
                    cursor_row++;
                }
            }
        }

        resolve_grid_tracks(state.columns, container_width, font_size, state.column_gap);
        resolve_grid_tracks(state.rows, container_height, font_size, state.row_gap);

        auto cell_start = [](const std::vector<GridTrackDef> &tracks, i32 index, f32 gap) -> f32 {
            f32 pos = 0;
            for (i32 i = 0; i < index; i++) {
                if (i > 0)
                    pos += gap;
                pos += tracks[i].resolved_size;
            }
            return pos;
        };

        auto cell_span_size = [](const std::vector<GridTrackDef> &tracks, i32 from, i32 count, f32 gap) -> f32 {
            f32 size = 0;
            for (i32 i = from; i < from + count; i++) {
                if (i > from)
                    size += gap;
                size += tracks[i].resolved_size;
            }
            return size;
        };

        struct GridAlign {
            JustifyContent justify;
            AlignItems align;
            bool stretch_w;
            bool stretch_h;
        };

        auto get_grid_align = [](const ComputedStyle &s) -> GridAlign {
            GridAlign ga{JustifyContent::FLEX_START, AlignItems::STRETCH, true, true};
            auto *js = s.get("justify-self");
            if (js && js->type == CSSValue::Type::KEYWORD) {
                if (js->keyword == "start" || js->keyword == "flex-start") {
                    ga.justify = JustifyContent::FLEX_START;
                    ga.stretch_w = false;
                } else if (js->keyword == "end" || js->keyword == "flex-end") {
                    ga.justify = JustifyContent::FLEX_END;
                    ga.stretch_w = false;
                } else if (js->keyword == "center") {
                    ga.justify = JustifyContent::CENTER;
                    ga.stretch_w = false;
                } else if (js->keyword == "stretch") {
                    ga.justify = JustifyContent::FLEX_START;
                    ga.stretch_w = true;
                }
            }
            auto *as = s.get("align-self");
            if (as && as->type == CSSValue::Type::KEYWORD) {
                if (as->keyword == "start" || as->keyword == "flex-start") {
                    ga.align = AlignItems::FLEX_START;
                    ga.stretch_h = false;
                } else if (as->keyword == "end" || as->keyword == "flex-end") {
                    ga.align = AlignItems::FLEX_END;
                    ga.stretch_h = false;
                } else if (as->keyword == "center") {
                    ga.align = AlignItems::CENTER;
                    ga.stretch_h = false;
                } else if (as->keyword == "stretch") {
                    ga.align = AlignItems::STRETCH;
                    ga.stretch_h = true;
                }
            }
            return ga;
        };

        std::unordered_set<LayoutNode *> sized_items;
        for (auto &[coord, cell] : state.cells) {
            if (sized_items.count(cell.item))
                continue;
            sized_items.insert(cell.item);

            auto [col, row] = coord;
            LayoutNode *item = cell.item;

            f32 cell_x = cell_start(state.columns, col, state.column_gap);
            f32 cell_y = cell_start(state.rows, row, state.row_gap);
            f32 cell_w = cell_span_size(state.columns, col, cell.col_span, state.column_gap);
            f32 cell_h = cell_span_size(state.rows, row, cell.row_span, state.row_gap);

            EdgeSizes item_margins;
            item_margins.left = resolve_side_value(item->style(), "margin-left", "margin", cell_w, font_size);
            item_margins.right = resolve_side_value(item->style(), "margin-right", "margin", cell_w, font_size);
            item_margins.top = resolve_side_value(item->style(), "margin-top", "margin", cell_h, font_size);
            item_margins.bottom = resolve_side_value(item->style(), "margin-bottom", "margin", cell_h, font_size);
            item->margin = item_margins;

            EdgeSizes item_borders;
            item_borders.left =
                resolve_side_value(item->style(), "border-left-width", "border-width", cell_w, font_size);
            item_borders.right =
                resolve_side_value(item->style(), "border-right-width", "border-width", cell_w, font_size);
            item_borders.top = resolve_side_value(item->style(), "border-top-width", "border-width", cell_h, font_size);
            item_borders.bottom =
                resolve_side_value(item->style(), "border-bottom-width", "border-width", cell_h, font_size);
            if (item_borders.top == 0) {
                auto *bv = item->style().get("border");
                if (bv && bv->type == CSSValue::Type::LENGTH) {
                    f32 bw = resolve_length(bv->length, cell_w, font_size);
                    item_borders = {bw, bw, bw, bw};
                }
            }
            item->border = item_borders;

            EdgeSizes item_paddings;
            item_paddings.left = resolve_side_value(item->style(), "padding-left", "padding", cell_w, font_size);
            item_paddings.right = resolve_side_value(item->style(), "padding-right", "padding", cell_w, font_size);
            item_paddings.top = resolve_side_value(item->style(), "padding-top", "padding", cell_h, font_size);
            item_paddings.bottom = resolve_side_value(item->style(), "padding-bottom", "padding", cell_h, font_size);
            item->padding = item_paddings;

            GridAlign ga = get_grid_align(item->style());

            f32 content_w = cell_w - item_margins.left - item_margins.right - item_borders.left - item_borders.right -
                            item_paddings.left - item_paddings.right;
            if (content_w < 0)
                content_w = 0;

            f32 content_h = cell_h - item_margins.top - item_margins.bottom - item_borders.top - item_borders.bottom -
                            item_paddings.top - item_paddings.bottom;
            if (content_h < 0)
                content_h = 0;

            if (!ga.stretch_h)
                content_h = 0;
            if (!ga.stretch_w)
                content_w = 0;

            item->content.width = content_w;
            item->content.height = content_h;

            item->content.x = cell_x + item_margins.left + item_borders.left + item_paddings.left;
            item->content.y = cell_y + item_margins.top + item_borders.top + item_paddings.top;

            if (!item->children.empty()) {
                if (is_flex_element(item->style())) {
                    f32 saved_x = item->content.x;
                    f32 saved_y = item->content.y;
                    layout_flex(item, content_w, content_h);
                    item->content.x = saved_x;
                    item->content.y = saved_y;
                    item->content.width = content_w;
                    item->content.height = content_h;
                } else if (is_grid_element(item->style())) {
                    f32 saved_x = item->content.x;
                    f32 saved_y = item->content.y;
                    layout_grid(item, content_w, content_h);
                    item->content.x = saved_x;
                    item->content.y = saved_y;
                    item->content.width = content_w;
                    item->content.height = content_h;
                } else {
                    layout_children(item, content_w, content_h);
                }
            }

            {
                f32 extra_w = cell_w - item->content.width - item_margins.left - item_margins.right -
                              item_borders.left - item_borders.right - item_paddings.left - item_paddings.right;
                if (extra_w > 0) {
                    if (ga.justify == JustifyContent::CENTER)
                        item->content.x += extra_w / 2;
                    else if (ga.justify == JustifyContent::FLEX_END)
                        item->content.x += extra_w;
                }
            }
            {
                f32 extra_h = cell_h - item->content.height - item_margins.top - item_margins.bottom -
                              item_borders.top - item_borders.bottom - item_paddings.top - item_paddings.bottom;
                if (extra_h > 0) {
                    if (ga.align == AlignItems::CENTER)
                        item->content.y += extra_h / 2;
                    else if (ga.align == AlignItems::FLEX_END)
                        item->content.y += extra_h;
                }
            }
        }

        f32 total_col_size = cell_start(state.columns, state.num_columns, state.column_gap);
        f32 total_row_size = cell_start(state.rows, state.num_rows, state.row_gap);

        node->content.width = total_col_size;
        node->content.height = total_row_size;

        if (wv && wv->type == CSSValue::Type::LENGTH) {
            node->content.width = resolve_length(wv->length, containing_width, font_size);
        }
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            node->content.height = resolve_length(hv->length, containing_height, font_size);
        }
    }

}  // namespace browser::css
