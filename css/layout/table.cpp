#include "../layout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace browser::css {

    namespace {

        bool is_table_cell_tag(const std::string &tag) {
            return tag == "td" || tag == "th";
        }

        bool is_table_row_tag(const std::string &tag) {
            return tag == "tr";
        }

        bool is_table_row_group_tag(const std::string &tag) {
            return tag == "thead" || tag == "tbody" || tag == "tfoot";
        }

    }  // namespace

    void LayoutEngine::layout_table(LayoutNode *node, f32 containing_width, f32 containing_height) {
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
        node->border = borders;

        f32 h_padding = paddings.left + paddings.right;
        f32 h_border = borders.left + borders.right;
        f32 h_margin = margins.left + margins.right;

        f32 available_width = containing_width - h_margin - h_padding - h_border;
        if (available_width < 0)
            available_width = 0;

        auto *wv = node->style().get("width");
        if (wv && wv->type == CSSValue::Type::LENGTH) {
            available_width = resolve_length(wv->length, containing_width, font_size);
        }
        node->content.width = available_width;

        struct TableCell {
            LayoutNode *cell = nullptr;
            i32 col = 0, row = 0;
            i32 colspan = 1, rowspan = 1;
            f32 min_width = 0;
            f32 content_height = 0;
        };
        struct TableRow {
            std::vector<TableCell> cells;
            LayoutNode *row_node = nullptr;
            LayoutNode *group_node = nullptr;
            f32 height = 0;
        };

        std::vector<TableRow> rows;

        auto collect_cells = [&](LayoutNode *row_node, LayoutNode *group_node) {
            TableRow row;
            row.row_node = row_node;
            row.group_node = group_node;
            for (auto &cell_child : row_node->children) {
                if (cell_child->is_text())
                    continue;
                html::Node *cn = cell_child->node();
                if (!cn || cn->type != html::NodeType::ELEMENT)
                    continue;
                auto *cel = static_cast<html::Element *>(cn);
                std::string ctag = cel->tag_name;
                for (auto &ccc : ctag) ccc = static_cast<char>(std::tolower(static_cast<unsigned char>(ccc)));

                if (is_table_cell_tag(ctag)) {
                    TableCell tc;
                    tc.cell = cell_child.get();
                    std::string cs = cel->get_attribute("colspan");
                    if (!cs.empty())
                        tc.colspan = std::max(1, static_cast<i32>(std::strtol(cs.c_str(), nullptr, 10)));
                    std::string rs = cel->get_attribute("rowspan");
                    if (!rs.empty())
                        tc.rowspan = std::max(1, static_cast<i32>(std::strtol(rs.c_str(), nullptr, 10)));
                    row.cells.push_back(tc);
                }
            }
            if (!row.cells.empty())
                rows.push_back(std::move(row));
        };

        for (auto &child : node->children) {
            if (child->is_text())
                continue;
            html::Node *n = child->node();
            if (!n || n->type != html::NodeType::ELEMENT)
                continue;
            auto *el = static_cast<html::Element *>(n);
            std::string tag = el->tag_name;
            for (auto &c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (is_table_row_tag(tag)) {
                collect_cells(child.get(), node);
            } else if (is_table_row_group_tag(tag)) {
                for (auto &row_child : child->children) {
                    if (row_child->is_text())
                        continue;
                    html::Node *rn = row_child->node();
                    if (!rn || rn->type != html::NodeType::ELEMENT)
                        continue;
                    auto *rel = static_cast<html::Element *>(rn);
                    std::string rtag = rel->tag_name;
                    for (auto &rc : rtag) rc = static_cast<char>(std::tolower(static_cast<unsigned char>(rc)));
                    if (is_table_row_tag(rtag)) {
                        collect_cells(row_child.get(), child.get());
                    }
                }
            } else if (is_table_cell_tag(tag)) {
                TableRow row;
                TableCell tc;
                tc.cell = child.get();
                tc.colspan = 1;
                tc.rowspan = 1;
                row.cells.push_back(tc);
                rows.push_back(std::move(row));
            }
        }

        if (rows.empty()) {
            node->content.height = 0;
            return;
        }

        i32 num_cols = 0;
        for (auto &row : rows) {
            i32 col = 0;
            for (auto &tc : row.cells) {
                col += tc.colspan;
            }
            if (col > num_cols)
                num_cols = col;
        }
        if (num_cols == 0)
            num_cols = 1;

        std::vector<f32> col_widths(num_cols, 0);
        std::vector<f32> col_min_widths(num_cols, 0);

        for (auto &row : rows) {
            i32 col = 0;
            for (auto &tc : row.cells) {
                f32 cell_available = available_width / static_cast<f32>(num_cols);
                if (tc.cell) {
                    layout_block(tc.cell, cell_available, containing_height);
                }
                f32 cell_content_w = tc.cell ? tc.cell->content.width + tc.cell->padding.left + tc.cell->padding.right +
                                                   tc.cell->border.left + tc.cell->border.right
                                             : 0;
                f32 per_col = cell_content_w / static_cast<f32>(tc.colspan);
                for (i32 c = 0; c < tc.colspan; c++) {
                    if (col + c < num_cols) {
                        if (per_col > col_widths[static_cast<size_t>(col + c)]) {
                            col_widths[static_cast<size_t>(col + c)] = per_col;
                        }
                    }
                }
                col += tc.colspan;
            }
        }

        f32 total_width = 0;
        for (auto &cw : col_widths) total_width += cw;
        if (total_width < available_width && total_width > 0) {
            f32 remaining = available_width - total_width;
            f32 extra_per_col = remaining / static_cast<f32>(num_cols);
            for (auto &cw : col_widths) cw += extra_per_col;
        } else if (total_width == 0) {
            f32 per_col = available_width / static_cast<f32>(num_cols);
            for (auto &cw : col_widths) cw = per_col;
        }

        f32 current_y = 0;

        struct SpanInfo {
            i32 remaining_rows;
            LayoutNode *cell;
            i32 col_start;
            i32 colspan;
        };
        std::vector<SpanInfo> active_rowspans;

        for (i32 ri = 0; ri < static_cast<i32>(rows.size()); ri++) {
            auto &row = rows[static_cast<size_t>(ri)];
            f32 row_height = 0;

            f32 current_x = 0;
            i32 col = 0;

            std::vector<SpanInfo> still_active;
            for (auto &span : active_rowspans) {
                span.remaining_rows--;
                if (span.remaining_rows > 0) {
                    still_active.push_back(span);
                }
                f32 span_width = 0;
                for (i32 c = span.col_start; c < span.col_start + span.colspan; c++) {
                    if (c < num_cols)
                        span_width += col_widths[static_cast<size_t>(c)];
                }
                current_x += span_width;
                col += span.colspan;
            }
            active_rowspans = still_active;

            for (auto &tc : row.cells) {
                while (col < num_cols) {
                    bool occupied = false;
                    for (auto &span : active_rowspans) {
                        if (col >= span.col_start && col < span.col_start + span.colspan) {
                            occupied = true;
                            break;
                        }
                    }
                    if (!occupied)
                        break;
                    for (auto &span : active_rowspans) {
                        if (col >= span.col_start && col < span.col_start + span.colspan) {
                            f32 span_width = 0;
                            for (i32 c = span.col_start; c < span.col_start + span.colspan; c++) {
                                if (c < num_cols)
                                    span_width += col_widths[static_cast<size_t>(c)];
                            }
                            current_x += span_width;
                            col += span.colspan;
                            break;
                        }
                    }
                }

                f32 cell_width = 0;
                for (i32 c = 0; c < tc.colspan; c++) {
                    if (col + c < num_cols)
                        cell_width += col_widths[static_cast<size_t>(col + c)];
                }

                if (tc.cell && tc.cell->node()) {
                    auto *cel = static_cast<html::Element *>(tc.cell->node());
                    std::string ctag = cel->tag_name;
                    for (auto &ccc : ctag) ccc = static_cast<char>(std::tolower(static_cast<unsigned char>(ccc)));

                    EdgeSizes cell_margins;
                    cell_margins.left =
                        resolve_side_value(tc.cell->style(), "margin-left", "margin", cell_width, font_size);
                    cell_margins.right =
                        resolve_side_value(tc.cell->style(), "margin-right", "margin", cell_width, font_size);
                    tc.cell->margin = cell_margins;

                    EdgeSizes cell_borders;
                    cell_borders.top =
                        resolve_side_value(tc.cell->style(), "border-top-width", "border-width", cell_width, font_size);
                    cell_borders.bottom = resolve_side_value(
                        tc.cell->style(), "border-bottom-width", "border-width", cell_width, font_size);
                    cell_borders.left = resolve_side_value(
                        tc.cell->style(), "border-left-width", "border-width", cell_width, font_size);
                    cell_borders.right = resolve_side_value(
                        tc.cell->style(), "border-right-width", "border-width", cell_width, font_size);
                    tc.cell->border = cell_borders;

                    EdgeSizes cell_paddings;
                    cell_paddings.left =
                        resolve_side_value(tc.cell->style(), "padding-left", "padding", cell_width, font_size);
                    cell_paddings.right =
                        resolve_side_value(tc.cell->style(), "padding-right", "padding", cell_width, font_size);
                    cell_paddings.top =
                        resolve_side_value(tc.cell->style(), "padding-top", "padding", cell_width, font_size);
                    cell_paddings.bottom =
                        resolve_side_value(tc.cell->style(), "padding-bottom", "padding", cell_width, font_size);
                    tc.cell->padding = cell_paddings;

                    f32 content_w = cell_width - cell_margins.left - cell_margins.right - cell_borders.left -
                                    cell_borders.right - cell_paddings.left - cell_paddings.right;
                    if (content_w < 0)
                        content_w = 0;

                    tc.cell->content.width = content_w;
                    tc.cell->content.x = current_x + cell_margins.left + cell_borders.left + cell_paddings.left;
                    tc.cell->content.y = current_y + cell_margins.top + cell_borders.top + cell_paddings.top;

                    if (!tc.cell->children.empty()) {
                        layout_children(tc.cell, content_w, containing_height);
                    }

                    f32 cell_h = tc.cell->content.height + tc.cell->padding.top + tc.cell->padding.bottom +
                                 tc.cell->border.top + tc.cell->border.bottom + tc.cell->margin.top +
                                 tc.cell->margin.bottom;
                    if (cell_h > row_height)
                        row_height = cell_h;
                }

                if (tc.rowspan > 1) {
                    active_rowspans.push_back({tc.rowspan - 1, tc.cell, col, tc.colspan});
                }

                current_x += cell_width;
                col += tc.colspan;
            }

            for (auto &tc : row.cells) {
                if (tc.cell) {
                    f32 cell_total_height = tc.cell->content.height + tc.cell->padding.top + tc.cell->padding.bottom +
                                            tc.cell->border.top + tc.cell->border.bottom + tc.cell->margin.top +
                                            tc.cell->margin.bottom;
                    if (row_height > cell_total_height) {
                        f32 extra = row_height - cell_total_height;
                        tc.cell->content.y += extra / 2;
                    }
                }
            }

            // Set the row LayoutNode's content rect
            if (row.row_node) {
                row.row_node->content.x = 0;
                row.row_node->content.y = current_y;
                row.row_node->content.width = available_width;
                row.row_node->content.height = row_height;
                row.row_node->padding = {0, 0, 0, 0};
                row.row_node->border = {0, 0, 0, 0};
                row.row_node->margin = {0, 0, 0, 0};
            }

            current_y += row_height;
        }

        // Union row-group rects from the row geometry
        for (auto &row : rows) {
            if (row.group_node && row.group_node != node) {
                if (row.group_node->content.height == 0 && row.row_node) {
                    row.group_node->content.x = 0;
                    row.group_node->content.y = row.row_node->content.y;
                    row.group_node->content.width = available_width;
                    row.group_node->content.height = row.row_node->content.height;
                    row.group_node->padding = {0, 0, 0, 0};
                    row.group_node->border = {0, 0, 0, 0};
                    row.group_node->margin = {0, 0, 0, 0};
                } else if (row.row_node) {
                    f32 old_bottom = row.group_node->content.y + row.group_node->content.height;
                    f32 row_bottom = row.row_node->content.y + row.row_node->content.height;
                    if (row_bottom > old_bottom)
                        row.group_node->content.height = row_bottom - row.group_node->content.y;
                }
            }
        }

        node->content.height = current_y;

        auto *hv = node->style().get("height");
        if (hv && hv->type == CSSValue::Type::LENGTH) {
            node->content.height = resolve_length(hv->length, containing_height, font_size);
        }
    }

}  // namespace browser::css
