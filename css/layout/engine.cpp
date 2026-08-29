#include "../../async/executor.hpp"
#include "../../html/traversal.hpp"
#include "../layout.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace browser::css {

    namespace {

        bool is_display_none(const ComputedStyle &style) {
            auto *v = style.get("display");
            return v && v->type == CSSValue::Type::KEYWORD && v->keyword == "none";
        }

    }  // namespace

    std::optional<Rect> Rect::intersect(const Rect &o) const {
        f32 l = std::max(x, o.x);
        f32 r = std::min(x + width, o.x + o.width);
        f32 t = std::max(y, o.y);
        f32 b = std::min(y + height, o.y + o.height);
        if (l < r && t < b) {
            return Rect{l, t, r - l, b - t};
        }
        return std::nullopt;
    }

    LayoutNode::LayoutNode(html::Element *element, ComputedStyle style)
        : node_(element), style_(std::move(style)), is_text_(false) {
        element_key_ = element ? element->tag_name + "_" + element->id() : "";
        if (element && !element->id().empty()) {
            std::ostringstream ss;
            ss << element;
            element_key_ += ss.str();
        }
    }

    LayoutNode::LayoutNode(const std::string &text, ComputedStyle style)
        : node_(nullptr), style_(std::move(style)), is_text_(true), text_(text) {}

    Rect LayoutNode::get_padding_box() const {
        return {content.x - padding.left,
                content.y - padding.top,
                content.width + padding.left + padding.right,
                content.height + padding.top + padding.bottom};
    }

    Rect LayoutNode::get_border_box() const {
        auto pb = get_padding_box();
        return {pb.x - border.left,
                pb.y - border.top,
                pb.width + border.left + border.right,
                pb.height + border.top + border.bottom};
    }

    Rect LayoutNode::get_margin_box() const {
        auto bb = get_border_box();
        return {bb.x - margin.left,
                bb.y - margin.top,
                bb.width + margin.left + margin.right,
                bb.height + margin.top + margin.bottom};
    }

    void LayoutNode::layout(f32, f32) {}

    void LayoutNode::set_position(f32 x, f32 y) {
        content.x = x;
        content.y = y;
    }

    std::unique_ptr<LayoutNode> LayoutEngine::make_anonymous_block(ComputedStyle style) {
        auto node = std::make_unique<LayoutNode>(static_cast<html::Element *>(nullptr), std::move(style));
        return node;
    }

    std::unique_ptr<LayoutNode> LayoutEngine::build_layout_tree(
        html::Node *node, const std::unordered_map<const html::Element *, ComputedStyle> &styles) {
        if (!node)
            return nullptr;

        if (node->type == html::NodeType::ELEMENT) {
            auto *el = static_cast<html::Element *>(node);
            auto it = styles.find(el);
            if (it == styles.end())
                return nullptr;
            if (is_display_none(it->second))
                return nullptr;

            auto layout_node = std::make_unique<LayoutNode>(el, it->second);

            std::vector<std::unique_ptr<LayoutNode>> inline_pending;

            {
                auto before_it = it->second.properties.find("_before_content");
                if (before_it != it->second.properties.end() && before_it->second.type == CSSValue::Type::STRING) {
                    std::string content = before_it->second.string_value;
                    if (content.size() >= 2 && content[0] == '"' && content.back() == '"') {
                        content = content.substr(1, content.size() - 2);
                    }
                    if (!content.empty()) {
                        ComputedStyle text_style = it->second;
                        {
                            CSSValue dv;
                            dv.type = CSSValue::Type::KEYWORD;
                            dv.keyword = "inline";
                            text_style.properties["display"] = dv;
                        }
                        auto text_node = std::make_unique<LayoutNode>(content, std::move(text_style));
                        inline_pending.push_back(std::move(text_node));
                    }
                }
            }

            for (auto &child : node->children) {
                if (child->type == html::NodeType::ELEMENT) {
                    auto *child_el = static_cast<html::Element *>(child.get());
                    auto child_it = styles.find(child_el);
                    if (child_it == styles.end())
                        continue;
                    if (is_display_none(child_it->second))
                        continue;

                    bool child_is_block = is_block_element(child_it->second);
                    bool child_is_inline_block = is_inline_element(child_it->second);

                    if (child_is_block && !child_is_inline_block) {
                        if (!inline_pending.empty()) {
                            auto anon = make_anonymous_block(it->second);
                            for (auto &r : inline_pending) {
                                r->parent = anon.get();
                                anon->children.push_back(std::move(r));
                            }
                            anon->parent = layout_node.get();
                            layout_node->children.push_back(std::move(anon));
                            inline_pending.clear();
                        }

                        auto child_node = build_layout_tree(child.get(), styles);
                        if (child_node) {
                            child_node->parent = layout_node.get();
                            layout_node->children.push_back(std::move(child_node));
                        }
                    } else {
                        auto child_node = build_layout_tree(child.get(), styles);
                        if (child_node) {
                            inline_pending.push_back(std::move(child_node));
                        }
                    }
                } else if (child->type == html::NodeType::TEXT) {
                    auto *text = static_cast<html::Text *>(child.get());
                    bool all_space = true;
                    for (char c : text->data) {
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                            all_space = false;
                            break;
                        }
                    }
                    if (all_space)
                        continue;

                    ComputedStyle text_style = it->second;
                    // Resolve font-size to absolute px so em values aren't double-resolved
                    {
                        f32 parent_fs = resolve_font_size(it->second, root_font_size_);
                        css::CSSValue pv;
                        pv.type = css::CSSValue::Type::LENGTH;
                        pv.length.value = parent_fs;
                        pv.length.unit = css::Length::Unit::PX;
                        text_style.properties["font-size"] = pv;
                    }
                    auto text_node = std::make_unique<LayoutNode>(text->data, std::move(text_style));
                    inline_pending.push_back(std::move(text_node));
                }
            }

            if (!inline_pending.empty()) {
                if (!layout_node->children.empty() && is_block_element(it->second)) {
                    auto anon = make_anonymous_block(it->second);
                    for (auto &r : inline_pending) {
                        r->parent = anon.get();
                        anon->children.push_back(std::move(r));
                    }
                    anon->parent = layout_node.get();
                    layout_node->children.push_back(std::move(anon));
                } else {
                    for (auto &r : inline_pending) {
                        r->parent = layout_node.get();
                        layout_node->children.push_back(std::move(r));
                    }
                }
                inline_pending.clear();
            }

            {
                auto after_it = it->second.properties.find("_after_content");
                if (after_it != it->second.properties.end() && after_it->second.type == CSSValue::Type::STRING) {
                    std::string content = after_it->second.string_value;
                    if (content.size() >= 2 && content[0] == '"' && content.back() == '"') {
                        content = content.substr(1, content.size() - 2);
                    }
                    if (!content.empty()) {
                        ComputedStyle text_style = it->second;
                        {
                            CSSValue dv;
                            dv.type = CSSValue::Type::KEYWORD;
                            dv.keyword = "inline";
                            text_style.properties["display"] = dv;
                        }
                        auto text_node = std::make_unique<LayoutNode>(content, std::move(text_style));
                        text_node->parent = layout_node.get();
                        layout_node->children.push_back(std::move(text_node));
                    }
                }
            }

            return layout_node;
        }

        if (node->type == html::NodeType::TEXT) {
            auto *text = static_cast<html::Text *>(node);
            bool all_space = true;
            for (char c : text->data) {
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    all_space = false;
                    break;
                }
            }
            if (all_space)
                return nullptr;

            ComputedStyle parent_style;
            html::Node *p = node->parent;
            while (p) {
                if (p->type == html::NodeType::ELEMENT) {
                    auto *pel = static_cast<html::Element *>(p);
                    auto it = styles.find(pel);
                    if (it != styles.end()) {
                        parent_style = it->second;
                        break;
                    }
                }
                p = p->parent;
            }

            return std::make_unique<LayoutNode>(text->data, std::move(parent_style));
        }

        return nullptr;
    }

    LayoutEngine::LayoutEngine() = default;

    async::task<std::unique_ptr<LayoutNode>> LayoutEngine::layout_async(
        html::Document *doc,
        const std::unordered_map<const html::Element *, ComputedStyle> &styles,
        f32 viewport_width,
        f32 viewport_height) {
        viewport_width_ = viewport_width;
        viewport_height_ = viewport_height;
        co_await async::thread_pool_executor{};

        auto *body = html::find_element_by_tag(doc, "body");
        if (!body) {
            body = html::find_element_by_tag(doc, "html");
        }
        if (!body)
            co_return nullptr;

        auto tree = build_layout_tree(body, styles);
        if (!tree)
            co_return nullptr;

        layout_block(tree.get(), viewport_width, viewport_height);

        tree->content.x = tree->margin.left + tree->border.left + tree->padding.left;
        tree->content.y = tree->margin.top + tree->border.top + tree->padding.top;

        f32 body_font_size = resolve_font_size(tree->style(), root_font_size_);
        layout_absolute_pass(tree.get(), nullptr, viewport_width, viewport_height, body_font_size);

        co_return tree;
    }

    void LayoutEngine::layout_children(LayoutNode *node, f32 containing_width, f32 containing_height) {
        if (!node || node->children.empty())
            return;
        if (is_flex_element(node->style()))
            return;
        if (is_grid_element(node->style()))
            return;

        bool has_block_child = false;
        bool has_inline_child = false;

        for (auto &child : node->children) {
            auto *pos = child->style().get("position");
            bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
            if (is_absolute)
                continue;

            if (child->is_text()) {
                has_inline_child = true;
            } else {
                if (is_block_element(child->style()))
                    has_block_child = true;
                else
                    has_inline_child = true;
            }
        }

        f32 float_left_x = 0;
        f32 float_right_x = containing_width;

        if (has_block_child) {
            f32 current_y = 0;
            f32 prev_margin_bottom = 0;
            bool first = true;

            for (auto &child : node->children) {
                auto *pos = child->style().get("position");
                bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (is_absolute)
                    continue;

                if (child->is_text()) {
                    layout_inline(child.get(), containing_width, containing_height);
                    child->content.x = 0;
                    child->content.y = current_y;
                    current_y += child->content.height;
                    continue;
                }

                layout_block(child.get(), containing_width, containing_height);

                if (child->is_floating) {
                    shrink_to_fit(child.get(), containing_width, containing_height);
                    // clear must be honored before placing this float, or the
                    // reset below is skipped by the continue.
                    auto *clear_val = child->style().get("clear");
                    if (clear_val && clear_val->type == CSSValue::Type::KEYWORD) {
                        if (clear_val->keyword == "left" || clear_val->keyword == "both")
                            float_left_x = 0;
                        if (clear_val->keyword == "right" || clear_val->keyword == "both")
                            float_right_x = containing_width;
                    }
                    f32 float_margin_box_w = child->content.width + child->padding.left + child->padding.right +
                                             child->border.left + child->border.right + child->margin.left +
                                             child->margin.right;
                    if (child->float_direction == 0) {
                        child->content.x = float_left_x;
                        float_left_x += float_margin_box_w;
                    } else {
                        // Successive right floats stack leftward from the edge.
                        float_right_x -= float_margin_box_w;
                        child->content.x = float_right_x;
                    }
                    child->content.y = current_y;
                    current_y += child->content.height;
                    continue;
                }

                f32 collapsed_gap;
                if (first) {
                    f32 parent_margin_top = node->margin.top;
                    bool parent_has_border_padding_top = node->border.top > 0 || node->padding.top > 0;
                    if (!parent_has_border_padding_top && parent_margin_top > 0) {
                        if (child->margin.top > parent_margin_top)
                            collapsed_gap = child->margin.top - parent_margin_top;
                        else
                            collapsed_gap = 0;
                    } else {
                        collapsed_gap = child->margin.top;
                    }
                } else {
                    collapsed_gap = std::max(prev_margin_bottom, child->margin.top);
                }

                child->content.x = child->margin.left + child->border.left + child->padding.left;

                auto *clear_val = child->style().get("clear");
                if (clear_val && clear_val->type == CSSValue::Type::KEYWORD) {
                    if (clear_val->keyword == "left" || clear_val->keyword == "both") {
                        float_left_x = 0;
                    }
                    if (clear_val->keyword == "right" || clear_val->keyword == "both") {
                        float_right_x = containing_width;
                    }
                }

                child->content.y = current_y + collapsed_gap + child->border.top + child->padding.top;
                current_y = child->content.y + child->content.height + child->padding.bottom + child->border.bottom;
                prev_margin_bottom = child->margin.bottom;
                first = false;
            }
        } else if (has_inline_child) {
            // Line-box model: every inline-level child is laid out at its
            // natural (shrink-to-fit) width first, then broken into lines and
            // positioned per the block's text-align.
            struct InlineItem {
                LayoutNode *node_ptr;
                f32 width = 0;   // margin-box width
                f32 height = 0;  // border-box height
                bool is_break = false;
            };
            std::vector<InlineItem> items;
            items.reserve(node->children.size());

            std::string text_align = "left";
            auto *ta = node->style().get("text-align");
            if (ta && ta->type == CSSValue::Type::KEYWORD)
                text_align = ta->keyword;

            auto is_display = [](LayoutNode *c, const char *kw) {
                auto *dv = c->style().get("display");
                return dv && dv->type == CSSValue::Type::KEYWORD && dv->keyword == kw;
            };
            f32 node_font_size = resolve_font_size(node->style(), root_font_size_);

            // Resolves an inline run's own box extras so padding/border on
            // inline elements participate in the line layout.
            auto resolve_run_box = [&](LayoutNode *run) {
                f32 pfs = root_font_size_;
                if (run->parent)
                    pfs = resolve_font_size(run->parent->style(), root_font_size_);
                f32 fs = resolve_font_size(run->style(), pfs);
                run->margin.top = resolve_side_value(run->style(), "margin-top", "margin", containing_width, fs);
                run->margin.bottom = resolve_side_value(run->style(), "margin-bottom", "margin", containing_width, fs);
                run->margin.left = resolve_side_value(run->style(), "margin-left", "margin", containing_width, fs);
                run->margin.right = resolve_side_value(run->style(), "margin-right", "margin", containing_width, fs);
                run->padding.top = resolve_side_value(run->style(), "padding-top", "padding", containing_width, fs);
                run->padding.bottom =
                    resolve_side_value(run->style(), "padding-bottom", "padding", containing_width, fs);
                run->padding.left = resolve_side_value(run->style(), "padding-left", "padding", containing_width, fs);
                run->padding.right = resolve_side_value(run->style(), "padding-right", "padding", containing_width, fs);
                run->border.top =
                    resolve_side_value(run->style(), "border-top-width", "border-width", containing_width, fs);
                run->border.bottom =
                    resolve_side_value(run->style(), "border-bottom-width", "border-width", containing_width, fs);
                run->border.left =
                    resolve_side_value(run->style(), "border-left-width", "border-width", containing_width, fs);
                run->border.right =
                    resolve_side_value(run->style(), "border-right-width", "border-width", containing_width, fs);
            };

            // Extra box width/height of a node (margin-box width, border-box height).
            auto extras_w = [](LayoutNode *c) {
                return c->margin.left + c->margin.right + c->padding.left + c->padding.right + c->border.left +
                       c->border.right;
            };
            auto extras_h = [](LayoutNode *c) {
                return c->padding.top + c->padding.bottom + c->border.top + c->border.bottom;
            };

            std::function<void(LayoutNode *)> layout_run_children = [&](LayoutNode *run) {
                for (auto &gc : run->children) {
                    auto *gpos = gc->style().get("position");
                    if (gpos && gpos->type == CSSValue::Type::KEYWORD && gpos->keyword == "absolute")
                        continue;
                    if (gc->is_text()) {
                        layout_inline(gc.get(), containing_width, containing_height, true);
                    } else if (is_display(gc.get(), "inline")) {
                        layout_run_children(gc.get());
                    } else {
                        layout_block(gc.get(), containing_width, containing_height);
                    }
                }
                run->content.width = 0;
                run->content.height = 0;
                for (auto &gc : run->children) {
                    auto *gpos = gc->style().get("position");
                    if (gpos && gpos->type == CSSValue::Type::KEYWORD && gpos->keyword == "absolute")
                        continue;
                    run->content.width += gc->content.width + extras_w(gc.get());
                    f32 gc_h = gc->content.height + extras_h(gc.get());
                    if (gc_h > run->content.height)
                        run->content.height = gc_h;
                }
            };

            for (auto &child : node->children) {
                auto *pos = child->style().get("position");
                bool is_absolute = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "absolute";
                if (is_absolute)
                    continue;

                InlineItem item;
                item.node_ptr = child.get();

                if (child->is_text()) {
                    layout_inline(child.get(), containing_width, containing_height, true);
                    item.width = child->content.width + child->margin.left + child->margin.right;
                    item.height = child->content.height;
                } else if (is_display(child.get(), "inline")) {
                    html::Node *cn = child->node();
                    auto *cel = cn && cn->type == html::NodeType::ELEMENT ? static_cast<html::Element *>(cn) : nullptr;
                    std::string ctag = cel ? cel->tag_name : "";
                    if (ctag == "br") {
                        // Forced line break: zero width, line-height tall.
                        f32 fs = resolve_font_size(child->style(), node_font_size);
                        f32 br_h = fs * 1.2f;
                        if (metrics_fn_) {
                            auto fm = metrics_fn_(metrics_ctx_, static_cast<u32>(fs));
                            br_h = fm.ascender - fm.descender + fm.line_gap;
                        }
                        child->content.width = 0;
                        child->content.height = br_h;
                        item.is_break = true;
                    } else {
                        resolve_run_box(child.get());
                        layout_run_children(child.get());
                    }
                    item.width = child->content.width + extras_w(child.get());
                    // CSS line boxes: vertical padding/border on non-atomic
                    // inline elements paint but do not grow the line height.
                    item.height = child->content.height;
                } else {
                    // inline-block or other inline-level box: full block layout
                    layout_block(child.get(), containing_width, containing_height);
                    if (is_display(child.get(), "inline-block"))
                        shrink_to_fit(child.get(), containing_width, containing_height);
                    item.width = child->content.width + extras_w(child.get());
                    item.height = child->content.height + extras_h(child.get());
                }
                items.push_back(item);
            }

            // Phase B/C: greedy line breaking, then position each line.
            f32 line_y = 0;
            size_t line_start = 0;
            f32 line_w = 0;

            auto flush_line = [&](size_t end_idx) {
                f32 total = 0;
                f32 lh = 0;
                for (size_t i = line_start; i < end_idx; i++) {
                    total += items[i].width;
                    if (items[i].height > lh)
                        lh = items[i].height;
                }
                f32 x0 = 0;
                if (text_align == "center")
                    x0 = (containing_width - total) / 2.0f;
                else if (text_align == "right")
                    x0 = containing_width - total;
                if (x0 < 0)
                    x0 = 0;
                f32 x = x0;
                for (size_t i = line_start; i < end_idx; i++) {
                    auto &it = items[i];
                    LayoutNode *c = it.node_ptr;
                    f32 c_x = x + c->margin.left + c->border.left + c->padding.left;
                    // vertical-align for atomic inline boxes
                    f32 c_y = line_y;
                    if (!c->is_text()) {
                        auto *va = c->style().get("vertical-align");
                        std::string vak = va && va->type == CSSValue::Type::KEYWORD ? va->keyword : "";
                        f32 box_h = it.height;
                        if (vak == "middle")
                            c_y = line_y + (lh - box_h) / 2.0f;
                        else if (vak == "bottom")
                            c_y = line_y + lh - box_h;
                        else if (vak == "sub")
                            c_y = line_y + lh * 0.2f;
                        else if (vak == "super")
                            c_y = line_y - lh * 0.3f;
                    }
                    c->content.x = c_x;
                    c->content.y = c_y;
                    x += it.width;
                }
                line_y += lh;
                line_start = end_idx;
            };

            for (size_t i = 0; i < items.size(); i++) {
                if (items[i].is_break) {
                    flush_line(i + 1);
                    line_w = 0;
                    continue;
                }
                if (line_w + items[i].width > containing_width && line_w > 0) {
                    flush_line(i);
                    line_w = 0;
                }
                line_w += items[i].width;
            }
            flush_line(items.size());

            node->content.width = containing_width;
            node->content.height = line_y;
        }
    }

}  // namespace browser::css
