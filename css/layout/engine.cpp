#include "../../async/executor.hpp"
#include "../../html/traversal.hpp"
#include "../layout.hpp"

#include <algorithm>
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
        html::Node *node, std::unordered_map<const html::Element *, ComputedStyle> &styles) {
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
        std::unordered_map<const html::Element *, ComputedStyle> &styles,
        f32 viewport_width,
        f32 viewport_height) {
        co_await async::thread_pool_executor{};
        viewport_width_ = viewport_width;
        viewport_height_ = viewport_height;

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
                    f32 float_margin_box_w = child->content.width + child->padding.left + child->padding.right +
                                             child->border.left + child->border.right + child->margin.left +
                                             child->margin.right;
                    if (child->float_direction == 0) {
                        child->content.x = float_left_x;
                        float_left_x += float_margin_box_w;
                    } else {
                        child->content.x = containing_width - float_margin_box_w;
                        float_right_x -= float_margin_box_w;
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

                if (child->is_text()) {
                    auto *ta = node->style().get("text-align");
                    if (ta && ta->type == CSSValue::Type::KEYWORD) {
                        f32 remaining = node->content.width - child->margin.left - child->border.left -
                                        child->padding.left - child->padding.right - child->border.right -
                                        child->margin.right - child->content.width;
                        if (ta->keyword == "center" && remaining > 0)
                            child->content.x += remaining / 2.0f;
                        else if (ta->keyword == "right" && remaining > 0)
                            child->content.x += remaining;
                    }
                }

                child->content.y = current_y + collapsed_gap;

                f32 child_border_bottom =
                    child->content.y + child->content.height + child->padding.bottom + child->border.bottom;
                current_y = child_border_bottom;
                prev_margin_bottom = child->margin.bottom;
                first = false;
            }
        } else if (has_inline_child) {
            f32 line_x = 0;
            f32 line_y = 0;
            f32 cur_line_height = 0;

            for (auto &child : node->children) {
                bool is_inline_block = false;
                if (!child->is_text()) {
                    auto *dv = child->style().get("display");
                    is_inline_block = dv && dv->type == CSSValue::Type::KEYWORD &&
                                      dv->keyword == "inline-block";
                }

                if (child->is_text()) {
                    layout_inline(child.get(), containing_width, containing_height);
                } else if (is_inline_block) {
                    // inline-block: layout as block, but inline-level
                    layout_block(child.get(), containing_width, containing_height);
                } else {
                    layout_block(child.get(), containing_width, containing_height);
                }

                if (line_x + child->content.width > containing_width && line_x > 0) {
                    line_y += cur_line_height;
                    line_x = 0;
                    cur_line_height = 0;
                }

                // Vertical alignment for inline-block
                f32 child_height = child->content.height + child->padding.top + child->padding.bottom +
                                   child->border.top + child->border.bottom + child->margin.top + child->margin.bottom;
                if (is_inline_block) {
                    auto *va = child->style().get("vertical-align");
                    bool va_middle = va && va->type == CSSValue::Type::KEYWORD && va->keyword == "middle";
                    bool va_top = va && va->type == CSSValue::Type::KEYWORD && va->keyword == "top";
                    bool va_bottom = va && va->type == CSSValue::Type::KEYWORD && va->keyword == "bottom";
                    if (va_middle) {
                        child->content.y = line_y + (cur_line_height - child_height) / 2.0f;
                    } else if (va_top) {
                        child->content.y = line_y;
                    } else if (va_bottom) {
                        child->content.y = line_y + cur_line_height - child_height;
                    } else {
                        // baseline: align bottom of inline-block with text baseline
                        child->content.y = line_y;
                    }
                } else {
                    child->content.y = line_y;
                }

                child->content.x = line_x;
                line_x += child->content.width;
                if (child_height > cur_line_height)
                    cur_line_height = child_height;
            }

            node->content.width = containing_width;
            node->content.height = line_y + cur_line_height;
        }
    }

}  // namespace browser::css
