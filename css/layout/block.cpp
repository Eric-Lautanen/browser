#include "../layout.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace browser::css {

    namespace {
        // Parses an element size attribute ("272", "272px", "25%"). Returns
        // false when absent or unparseable.
        bool parse_attr_size(const html::Element *el, const char *name, f32 &out, bool &is_percent) {
            std::string v = el->get_attribute(name);
            if (v.empty())
                return false;
            char *end = nullptr;
            f32 n = std::strtof(v.c_str(), &end);
            if (end == v.c_str() || n <= 0)
                return false;
            while (*end == ' ') end++;
            if (*end == '%') {
                is_percent = true;
            } else {
                is_percent = false;
            }
            out = n;
            return true;
        }
    }  // namespace

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
        } else if (wv->type == CSSValue::Type::PERCENTAGE) {
            width = wv->number / 100.0f * containing_width;
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
        if (maxw) {
            f32 mw = 0;
            if (maxw->type == CSSValue::Type::LENGTH)
                mw = resolve_length(maxw->length, containing_width, font_size);
            else if (maxw->type == CSSValue::Type::STRING || maxw->type == CSSValue::Type::FUNCTION)
                mw = resolve_func_length(node->style(), maxw, containing_width, font_size);
            if (mw > 0 && node->content.width > mw) {
                node->content.width = mw;
                had_maxw = true;
            }
        }
        auto *minw = node->style().get("min-width");
        if (minw) {
            f32 mw = 0;
            if (minw->type == CSSValue::Type::LENGTH)
                mw = resolve_length(minw->length, containing_width, font_size);
            else if (minw->type == CSSValue::Type::STRING || minw->type == CSSValue::Type::FUNCTION)
                mw = resolve_func_length(node->style(), minw, containing_width, font_size);
            else if (minw->type == CSSValue::Type::PERCENTAGE)
                mw = minw->number / 100.0f * containing_width;
            if (mw > 0 && node->content.width < mw)
                node->content.width = mw;
        }

        auto *ml = node->style().get("margin-left");
        auto *mr = node->style().get("margin-right");
        bool ml_auto = !ml || (ml->type == CSSValue::Type::KEYWORD && ml->keyword == "auto");
        bool mr_auto = !mr || (mr->type == CSSValue::Type::KEYWORD && mr->keyword == "auto");
        bool has_fixed_width = wv && (wv->type == CSSValue::Type::LENGTH || wv->type == CSSValue::Type::PERCENTAGE ||
                                      wv->type == CSSValue::Type::STRING || wv->type == CSSValue::Type::FUNCTION);
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

        // Float state is resolved before the formatting-context dispatches so
        // floated flex/grid/table containers also take the float path in
        // their parent's layout_children.
        {
            auto *float_val = node->style().get("float");
            bool floating = float_val && float_val->type == CSSValue::Type::KEYWORD &&
                            (float_val->keyword == "left" || float_val->keyword == "right");
            node->is_floating = floating;
            node->float_direction = floating && float_val->keyword == "left" ? 0 : 1;
        }

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
            // Only actual tables use the table formatter; cells and rows are
            // laid out by layout_table itself (or fall back to block flow when
            // reached outside a table context).
            auto *tdisp = node->style().get("display");
            std::string tkind = tdisp && tdisp->type == CSSValue::Type::KEYWORD ? tdisp->keyword : "";
            if (tkind == "table" || tkind == "inline-table") {
                layout_table(node, containing_width, containing_height);
                return;
            }
        }

        if (is_flex_element(node->style())) {
            layout_flex(node, containing_width, containing_height);
            // A floated or inline-level flex container shrink-to-fits its
            // content instead of stretching to the containing block.
            auto *fdisp = node->style().get("display");
            bool inline_flex =
                fdisp && fdisp->type == CSSValue::Type::KEYWORD && fdisp->keyword == "inline-flex";
            if (width_auto && (node->is_floating || inline_flex) && !node->children.empty()) {
                auto extras_of = [](LayoutNode *c) {
                    return c->margin.left + c->margin.right + c->padding.left + c->padding.right + c->border.left +
                           c->border.right;
                };
                auto *fdir = node->style().get("flex-direction");
                bool is_column = fdir && fdir->type == CSSValue::Type::KEYWORD &&
                                 (fdir->keyword == "column" || fdir->keyword == "column-reverse");
                f32 natural = 0;
                if (is_column) {
                    for (auto &c : node->children) {
                        f32 w = c->content.width + extras_of(c.get());
                        if (w > natural)
                            natural = w;
                    }
                } else {
                    for (auto &c : node->children) {
                        f32 end = c->content.x + c->content.width + c->margin.right + c->padding.right +
                                  c->border.right;
                        if (end > natural)
                            natural = end;
                    }
                }
                if (natural > 0 && natural < node->content.width) {
                    node->content.width = natural;
                    layout_flex(node, natural, containing_height);
                }
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
            return;
        }

        // Multi-column layout
        bool has_columns = false;
        f32 column_width = 0;
        int column_count = 0;
        f32 column_gap_val = 0;
        {
            auto *cw = node->style().get("column-width");
            auto *cc = node->style().get("column-count");
            auto *cg = node->style().get("column-gap");
            if (cw || cc) {
                has_columns = true;
                if (cw && cw->type == CSSValue::Type::LENGTH) {
                    column_width = resolve_length(cw->length, containing_width, font_size);
                }
                if (cc && cc->type == CSSValue::Type::NUMBER) {
                    column_count = static_cast<int>(cc->number);
                }
                if (cg && cg->type == CSSValue::Type::LENGTH) {
                    column_gap_val = resolve_length(cg->length, containing_width, font_size);
                } else if (cg && cg->type == CSSValue::Type::NUMBER) {
                    column_gap_val = cg->number;
                }
                if (column_gap_val <= 0)
                    column_gap_val = font_size;
            }
        }

        if (has_columns && !node->children.empty()) {
            // Compute number of columns
            f32 content_width = node->content.width;
            int num_cols = column_count;
            if (column_width > 0 && num_cols <= 0) {
                num_cols = static_cast<int>(content_width / (column_width + column_gap_val));
                if (num_cols < 1)
                    num_cols = 1;
            } else if (num_cols <= 0) {
                num_cols = 1;
            }
            f32 total_gap = (num_cols - 1) * column_gap_val;
            f32 col_width = (content_width - total_gap) / num_cols;
            if (col_width < 20.0f) {
                col_width = 20.0f;
                num_cols = static_cast<int>((content_width + column_gap_val) / (col_width + column_gap_val));
                if (num_cols < 1)
                    num_cols = 1;
            }

            // Lay out children into columns
            f32 col_y = 0;
            int current_col = 0;
            f32 current_x = 0;
            f32 max_col_h = 0;

            for (auto &child : node->children) {
                auto *pos = child->style().get("position");
                bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (is_absolute)
                    continue;

                layout_block(child.get(), col_width, containing_height);

                if (col_y + child->content.height > containing_height && col_y > 0 && current_col < num_cols - 1) {
                    // Move to next column
                    current_col++;
                    current_x += col_width + column_gap_val;
                    col_y = 0;
                }

                child->content.x = current_x;
                child->content.y = col_y;
                col_y += child->content.height + child->margin.bottom + child->border.bottom + child->padding.bottom;
                if (col_y > max_col_h)
                    max_col_h = col_y;
            }

            node->content.height = max_col_h;

            // Apply column-rule rendering info (stored in style for painter)
            auto *crw = node->style().get("column-rule-width");
            auto *crs = node->style().get("column-rule-style");
            auto *crc = node->style().get("column-rule-color");
            if (!crw && !crs && !crc) {
                // No column-rule specified
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
                } else if (tag == "select") {
                    if (width_auto) {
                        // The width should fit the longest option text (plus
                        // a 20-px dropdown arrow + 6 px padding). Using
                        // el->get_attribute("value") gives 0 since <select>
                        // rarely has a value attribute, which made selects
                        // collapse to 30 px wide.
                        f32 max_tw = 0.0f;
                        std::function<void(html::Node *)> scan = [&](html::Node *parent) {
                            for (auto &c : parent->children) {
                                if (c->type != html::NodeType::ELEMENT)
                                    continue;
                                auto *ch = static_cast<html::Element *>(c.get());
                                if (ch->tag_name == "option") {
                                    std::string opt_text;
                                    for (auto &tc : ch->children) {
                                        if (tc->type == html::NodeType::TEXT)
                                            opt_text += static_cast<html::Text *>(tc.get())->data;
                                    }
                                    if (opt_text.empty())
                                        opt_text = ch->get_attribute("value");
                                    if (opt_text.empty())
                                        opt_text = ch->get_attribute("label");
                                    f32 tw = static_cast<f32>(opt_text.size()) * 7.0f;
                                    if (tw > max_tw)
                                        max_tw = tw;
                                } else if (ch->tag_name == "optgroup") {
                                    scan(ch);
                                }
                            }
                        };
                        scan(el);
                        node->content.width = std::max(max_tw + 30.0f, 50.0f);
                    }
                    if (node->content.height == 0)
                        node->content.height = 20.0f;
                } else if (tag == "button" || (tag == "input" && type == "submit")) {
                    if (width_auto) {
                        // Pick the longest of: value attribute, inner text
                        std::string label = el->get_attribute("value");
                        std::string inner;
                        for (auto &tc : el->children) {
                            if (tc->type == html::NodeType::TEXT)
                                inner += static_cast<html::Text *>(tc.get())->data;
                        }
                        if (inner.size() > label.size())
                            label = inner;
                        if (label.empty())
                            label = (type == "reset") ? "Reset" : (tag == "button" ? "" : "Submit");
                        node->content.width = static_cast<f32>(label.size()) * 7.0f + 20.0f;
                        if (node->content.width < 60.0f)
                            node->content.width = 60.0f;  // mainstream minimum
                    }
                    if (node->content.height == 0)
                        node->content.height = font_size + 8.0f;
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

        // <summary> needs padding on the left so the disclosure triangle
        // drawn by the painter sits clear of the summary text. (Markers
        // don't apply because the default display for summary is `block`.)
        if (node->node() && node->node()->type == html::NodeType::ELEMENT) {
            auto *own_el = static_cast<html::Element *>(node->node());
            if (own_el->tag_name == "summary") {
                f32 disclosure_w = font_size * 1.2f;
                if (node->padding.left < disclosure_w)
                    node->padding.left = disclosure_w;
            }
        }

        auto *overflow = node->style().get("overflow");
        if (overflow && overflow->type == CSSValue::Type::KEYWORD) {
            if (overflow->keyword == "scroll" || overflow->keyword == "auto") {
                node->is_scrollable = true;
            }
        }

        layout_children(node, node->content.width, containing_height);

        auto *hv = node->style().get("height");
        if (hv) {
            f32 h = 0;
            if (hv->type == CSSValue::Type::LENGTH)
                h = resolve_length(hv->length, containing_height, font_size);
            else if (hv->type == CSSValue::Type::STRING || hv->type == CSSValue::Type::FUNCTION)
                h = resolve_func_length(node->style(), hv, containing_height, font_size);
            else if (hv->type == CSSValue::Type::PERCENTAGE)
                h = hv->number / 100.0f * containing_height;
            if (h > 0)
                node->content.height = h;
        } else {
            f32 max_y = 0;
            for (auto &child : node->children) {
                auto *pos = child->style().get("position");
                bool abs_pos = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (abs_pos)
                    continue;
                // Non-atomic inline children: their vertical padding/border
                // paints outside the line box and does not grow the parent.
                bool atomic = true;
                if (!child->is_text()) {
                    auto *d = child->style().get("display");
                    atomic = !(d && d->type == CSSValue::Type::KEYWORD && d->keyword == "inline");
                }
                f32 child_bottom = child->content.y + child->content.height +
                                   (atomic ? child->padding.bottom + child->border.bottom + child->margin.bottom : 0);
                if (child_bottom > max_y)
                    max_y = child_bottom;
            }
            node->content.height = max_y;
        }

        // Apply aspect-ratio if one dimension is auto
        auto *ar = node->style().get("aspect-ratio");
        if (ar) {
            f32 ratio = 0;
            if (ar->type == CSSValue::Type::NUMBER) {
                ratio = ar->number;
            } else if (ar->type == CSSValue::Type::STRING) {
                // Parse "w / h" or "w"
                std::string s = ar->string_value;
                size_t slash = s.find('/');
                if (slash != std::string::npos) {
                    f32 w = std::strtof(s.c_str(), nullptr);
                    f32 h = std::strtof(s.c_str() + slash + 1, nullptr);
                    if (w > 0 && h > 0)
                        ratio = w / h;
                } else {
                    ratio = std::strtof(s.c_str(), nullptr);
                }
            } else if (ar->type == CSSValue::Type::KEYWORD && ar->keyword == "auto") {
                // auto: use intrinsic ratio if available (not implemented)
            }
            if (ratio > 0) {
                auto *hv = node->style().get("height");
                bool height_auto = !hv || (hv->type == CSSValue::Type::KEYWORD && hv->keyword == "auto");
                if (!width_auto && height_auto) {
                    node->content.height = node->content.width / ratio;
                } else if (width_auto && !height_auto && node->content.height > 0) {
                    node->content.width = node->content.height * ratio;
                }
            }
        }

        auto *maxh = node->style().get("max-height");
        if (maxh) {
            f32 mh = 0;
            if (maxh->type == CSSValue::Type::LENGTH)
                mh = resolve_length(maxh->length, containing_height, font_size);
            else if (maxh->type == CSSValue::Type::STRING || maxh->type == CSSValue::Type::FUNCTION)
                mh = resolve_func_length(node->style(), maxh, containing_height, font_size);
            if (mh > 0 && node->content.height > mh)
                node->content.height = mh;
        }
        auto *minh = node->style().get("min-height");
        if (minh) {
            f32 mh = 0;
            if (minh->type == CSSValue::Type::LENGTH)
                mh = resolve_length(minh->length, containing_height, font_size);
            else if (minh->type == CSSValue::Type::STRING || minh->type == CSSValue::Type::FUNCTION)
                mh = resolve_func_length(node->style(), minh, containing_height, font_size);
            else if (minh->type == CSSValue::Type::PERCENTAGE)
                mh = minh->number / 100.0f * containing_height;
            if (mh > 0 && node->content.height < mh)
                node->content.height = mh;
        }

        // Replaced elements (img/svg/video/canvas): intrinsic + attribute sizing.
        size_replaced_element(node, containing_width, containing_height, font_size, width_auto);

        apply_transform_to_node(node);
    }

    // CSS 2.1 §10.3.2 replaced-element sizing: CSS width/height win; missing
    // sides come from width/height attributes (percentages resolve against the
    // containing block) and finally from the intrinsic image size, preserving
    // the aspect ratio when exactly one dimension is specified.
    void LayoutEngine::size_replaced_element(
        LayoutNode *node, f32 containing_width, f32 containing_height, f32 font_size, bool width_auto) {
        (void)font_size;
        html::Node *n = node->node();
        if (!n || n->type != html::NodeType::ELEMENT)
            return;
        auto *el = static_cast<html::Element *>(n);
        std::string tag = el->tag_name;
        for (auto &c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tag != "img" && tag != "svg" && tag != "video" && tag != "canvas")
            return;

        f32 nat_w = 0, nat_h = 0;
        if (tag == "img") {
            std::string key = el->resolved_src.empty() ? el->get_attribute("src") : el->resolved_src;
            auto it = image_sizes_.find(key);
            if (it != image_sizes_.end()) {
                nat_w = it->second.first;
                nat_h = it->second.second;
            }
        }

        f32 attr_w = 0, attr_h = 0;
        bool wp = false, hp = false;
        bool has_aw = parse_attr_size(el, "width", attr_w, wp);
        bool has_ah = parse_attr_size(el, "height", attr_h, hp);
        if (tag == "svg") {
            // SVG defaults to 300x150 when neither attrs nor CSS size it.
            if (!has_aw) {
                attr_w = 300;
                has_aw = true;
                wp = false;
            }
            if (!has_ah) {
                attr_h = 150;
                has_ah = true;
                hp = false;
            }
        }
        if (nat_w <= 0 && has_aw && !wp)
            nat_w = attr_w;
        if (nat_h <= 0 && has_ah && !hp)
            nat_h = attr_h;
        f32 ratio = (nat_w > 0 && nat_h > 0) ? nat_w / nat_h : 0.0f;

        if (width_auto) {
            f32 w = 0;
            if (has_aw)
                w = wp ? attr_w / 100.0f * containing_width : attr_w;
            if (w <= 0)
                w = nat_w;
            if (w > 0)
                node->content.width = w;
        }

        auto *css_h_v = node->style().get("height");
        bool has_css_h = css_h_v != nullptr;

        if (!has_css_h) {
            f32 used_w = node->content.width;
            f32 h = 0;
            if (!width_auto) {
                // CSS width fixed: derive height from the aspect ratio.
                if (ratio > 0)
                    h = used_w / ratio;
            } else if (has_ah) {
                h = hp ? attr_h / 100.0f * containing_height : attr_h;
                if (h <= 0 && ratio > 0 && used_w > 0)
                    h = used_w / ratio;
            } else if (has_aw && ratio > 0 && used_w > 0) {
                h = used_w / ratio;
            } else {
                h = nat_h;
            }
            if (h > 0)
                node->content.height = h;
        } else if (width_auto && ratio > 0 && node->content.height > 0) {
            node->content.width = node->content.height * ratio;
        }
    }

    // Floats and inline-blocks size to their content (CSS basic box model
    // shrink-to-fit): shrink descendants first, measure the widest content
    // line, then re-flow children at the final width. Best effort — mixed
    // inline/block nesting approximates a single line per run.
    void LayoutEngine::shrink_to_fit(LayoutNode *box, f32 containing_width, f32 containing_height) {
        if (!box)
            return;
        // Leaves (replaced elements, form controls) keep their computed width;
        // ancestors shrink to them.
        if (box->children.empty())
            return;

        auto extras_of = [](LayoutNode *c) {
            return c->margin.left + c->margin.right + c->padding.left + c->padding.right + c->border.left +
                   c->border.right;
        };

        // Post-order: descendants settle at natural width first (including
        // inline elements, whose nested blocks would otherwise stay stretched
        // and poison the run measurement).
        for (auto &c : box->children) {
            auto *pos = c->style().get("position");
            if (pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute")
                continue;
            if (!c->is_text())
                shrink_to_fit(c.get(), box->content.width, containing_height);
        }

        f32 run = 0, best = 0;
        for (auto &c : box->children) {
            auto *pos = c->style().get("position");
            if (pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute")
                continue;
            if (c->is_text()) {
                f32 fs = resolve_font_size(c->style(), root_font_size_);
                f32 natural = text_measure_fn_ ? text_measure_fn_(text_measurer_ctx_, c->text(), static_cast<u32>(fs))
                                               : c->content.width;
                if (natural <= 0)
                    natural = c->content.width;
                run += std::min(natural, containing_width);
            } else if (is_inline_element(c->style())) {
                run += c->content.width + extras_of(c.get());
            } else {
                best = std::max(best, run);
                run = 0;
                best = std::max(best, c->content.width + extras_of(c.get()));
            }
        }
        best = std::max(best, run);

        // An explicit CSS width/height wins over the shrink-to-fit result.
        auto *css_w = box->style().get("width");
        bool has_css_w = css_w && !(css_w->type == CSSValue::Type::KEYWORD && css_w->keyword == "auto");
        auto *css_h = box->style().get("height");
        bool has_css_h = css_h != nullptr;

        bool shrunk = best > 0 && best < box->content.width && !has_css_w;
        if (shrunk)
            box->content.width = best;
        if (shrunk) {
            layout_children(box, box->content.width, containing_height);
            f32 max_y = 0;
            for (auto &c : box->children) {
                auto *pos = c->style().get("position");
                if (pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute")
                    continue;
                bool atomic = true;
                if (!c->is_text()) {
                    auto *d = c->style().get("display");
                    atomic = !(d && d->type == CSSValue::Type::KEYWORD && d->keyword == "inline");
                }
                f32 bottom = c->content.y + c->content.height +
                             (atomic ? c->padding.bottom + c->border.bottom + c->margin.bottom : 0);
                if (bottom > max_y)
                    max_y = bottom;
            }
            if (max_y > 0 && !has_css_h)
                box->content.height = max_y;
        }
    }

}  // namespace browser::css
