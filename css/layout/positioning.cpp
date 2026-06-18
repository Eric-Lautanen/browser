#include "../layout.hpp"

#include <algorithm>

namespace browser::css {

    namespace {

        f32 deg_to_rad(f32 deg) {
            return deg * 3.14159265f / 180.0f;
        }

    }  // namespace

    void apply_transform_to_node(LayoutNode *node) {
        auto *tr = node->style().get("transform");
        if (!tr || tr->type != CSSValue::Type::TRANSFORM || tr->transforms.empty())
            return;

        auto *to = node->style().get("transform-origin");
        f32 origin_x = node->content.width / 2.0f;
        f32 origin_y = node->content.height / 2.0f;
        if (to && to->type == CSSValue::Type::STRING) {
            std::string s = to->string_value;
            char *end = nullptr;
            f32 v1 = std::strtof(s.c_str(), &end);
            if (end && end != s.c_str()) {
                origin_x = v1;
                std::string rest = end;
                while (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
                f32 v2 = std::strtof(rest.c_str(), &end);
                if (end && end != rest.c_str()) {
                    origin_y = v2;
                }
            } else {
                if (s.find("left") != std::string::npos)
                    origin_x = 0;
                else if (s.find("right") != std::string::npos)
                    origin_x = node->content.width;
                else
                    origin_x = node->content.width / 2;

                if (s.find("top") != std::string::npos)
                    origin_y = 0;
                else if (s.find("bottom") != std::string::npos)
                    origin_y = node->content.height;
                else
                    origin_y = node->content.height / 2;
            }
        }

        node->transform_matrix = Mat3x3::identity();
        node->transform_matrix = node->transform_matrix.translate(origin_x, origin_y);

        for (const auto &tf : tr->transforms) {
            f32 a = tf.args.size() > 0 ? tf.args[0] : 0;
            f32 b = tf.args.size() > 1 ? tf.args[1] : 0;
            f32 c = tf.args.size() > 2 ? tf.args[2] : 0;
            f32 d = tf.args.size() > 3 ? tf.args[3] : 0;
            f32 e = tf.args.size() > 4 ? tf.args[4] : 0;
            f32 f = tf.args.size() > 5 ? tf.args[5] : 0;

            switch (tf.type) {
                case TransformFunc::Type::TRANSLATE:
                    node->transform_matrix = node->transform_matrix.translate(a, b);
                    break;
                case TransformFunc::Type::TRANSLATE_X:
                    node->transform_matrix = node->transform_matrix.translate(a, 0);
                    break;
                case TransformFunc::Type::TRANSLATE_Y:
                    node->transform_matrix = node->transform_matrix.translate(0, a);
                    break;
                case TransformFunc::Type::SCALE:
                    node->transform_matrix = node->transform_matrix.scale(a, b > 0 ? b : a);
                    break;
                case TransformFunc::Type::SCALE_X:
                    node->transform_matrix = node->transform_matrix.scale(a, 1);
                    break;
                case TransformFunc::Type::SCALE_Y:
                    node->transform_matrix = node->transform_matrix.scale(1, a);
                    break;
                case TransformFunc::Type::ROTATE:
                    node->transform_matrix = node->transform_matrix.rotate(deg_to_rad(a));
                    break;
                case TransformFunc::Type::SKEW:
                    node->transform_matrix = node->transform_matrix.skew(deg_to_rad(a), deg_to_rad(b));
                    break;
                case TransformFunc::Type::SKEW_X:
                    node->transform_matrix = node->transform_matrix.skew(deg_to_rad(a), 0);
                    break;
                case TransformFunc::Type::SKEW_Y:
                    node->transform_matrix = node->transform_matrix.skew(0, deg_to_rad(a));
                    break;
                case TransformFunc::Type::MATRIX:
                    node->transform_matrix = node->transform_matrix.multiply(Mat3x3::from_values(a, b, c, d, e, f));
                    break;
            }
        }

        node->transform_matrix = node->transform_matrix.translate(-origin_x, -origin_y);
        node->has_transform = true;
    }

    void LayoutEngine::layout_absolute_pass(
        LayoutNode *node, LayoutNode *containing_block, f32 cb_width, f32 cb_height, f32 font_size) {
        if (!node)
            return;

        auto *pos = node->style().get("position");
        bool is_relative = pos && pos->type == CSSValue::Type::KEYWORD && pos->keyword == "relative";

        if (is_relative) {
            if (node->style().get("left"))
                node->content.x += resolve_property(node->style(), "left", cb_width, font_size);
            else if (node->style().get("right"))
                node->content.x -= resolve_property(node->style(), "right", cb_width, font_size);
            if (node->style().get("top"))
                node->content.y += resolve_property(node->style(), "top", cb_height, font_size);
            else if (node->style().get("bottom"))
                node->content.y -= resolve_property(node->style(), "bottom", cb_height, font_size);
        }

        for (auto &child : node->children) {
            auto *child_pos = child->style().get("position");
            bool child_is_absolute =
                child_pos && child_pos->type == CSSValue::Type::KEYWORD && child_pos->keyword == "absolute";
            bool child_is_fixed =
                child_pos && child_pos->type == CSSValue::Type::KEYWORD && child_pos->keyword == "fixed";
            bool child_is_sticky =
                child_pos && child_pos->type == CSSValue::Type::KEYWORD && child_pos->keyword == "sticky";

            f32 child_cb_width = cb_width;
            f32 child_cb_height = cb_height;
            f32 child_font_size = font_size;

            if (child_is_absolute || child_is_fixed) {
                LayoutNode *child_cb = nullptr;
                if (child_is_fixed) {
                    child_cb_width = viewport_width_;
                    child_cb_height = viewport_height_;
                    child_font_size = root_font_size_;

                    layout_block(child.get(), child_cb_width, child_cb_height);

                    bool has_left = child->style().get("left") != nullptr;
                    bool has_right = child->style().get("right") != nullptr;
                    bool has_top = child->style().get("top") != nullptr;
                    bool has_bottom = child->style().get("bottom") != nullptr;

                    if (has_left) {
                        f32 left_off = resolve_property(child->style(), "left", child_cb_width, child_font_size);
                        child->content.x = left_off;
                    } else if (has_right) {
                        f32 right_off = resolve_property(child->style(), "right", child_cb_width, child_font_size);
                        child->content.x = child_cb_width - right_off - child->content.width;
                    }

                    if (has_top) {
                        f32 top_off = resolve_property(child->style(), "top", child_cb_height, child_font_size);
                        child->content.y = top_off;
                    } else if (has_bottom) {
                        f32 bottom_off = resolve_property(child->style(), "bottom", child_cb_height, child_font_size);
                        child->content.y = child_cb_height - bottom_off - child->content.height;
                    }
                } else {
                    child_cb = find_positioned_ancestor(child.get());
                    if (child_cb) {
                        auto cb_padding = child_cb->get_padding_box();
                        child_cb_width = cb_padding.width;
                        child_cb_height = cb_padding.height;
                        child_font_size = resolve_font_size(child_cb->style(), font_size);

                        layout_block(child.get(), child_cb_width, child_cb_height);

                        bool has_left = child->style().get("left") != nullptr;
                        bool has_right = child->style().get("right") != nullptr;
                        bool has_top = child->style().get("top") != nullptr;
                        bool has_bottom = child->style().get("bottom") != nullptr;

                        if (has_left) {
                            f32 left_off = resolve_property(child->style(), "left", child_cb_width, child_font_size);
                            child->content.x = cb_padding.x + left_off;
                        } else if (has_right) {
                            f32 right_off = resolve_property(child->style(), "right", child_cb_width, child_font_size);
                            child->content.x = cb_padding.x + cb_padding.width - right_off - child->content.width;
                        }

                        if (has_top) {
                            f32 top_off = resolve_property(child->style(), "top", child_cb_height, child_font_size);
                            child->content.y = cb_padding.y + top_off;
                        } else if (has_bottom) {
                            f32 bottom_off =
                                resolve_property(child->style(), "bottom", child_cb_height, child_font_size);
                            child->content.y = cb_padding.y + cb_padding.height - bottom_off - child->content.height;
                        }
                    } else {
                        child_cb_width = viewport_width_;
                        child_cb_height = viewport_height_;

                        layout_block(child.get(), child_cb_width, child_cb_height);

                        bool has_left = child->style().get("left") != nullptr;
                        bool has_right = child->style().get("right") != nullptr;
                        bool has_top = child->style().get("top") != nullptr;
                        bool has_bottom = child->style().get("bottom") != nullptr;

                        if (has_left) {
                            f32 left_off = resolve_property(child->style(), "left", child_cb_width, root_font_size_);
                            child->content.x = left_off;
                        } else if (has_right) {
                            f32 right_off = resolve_property(child->style(), "right", child_cb_width, root_font_size_);
                            child->content.x = child_cb_width - right_off - child->content.width;
                        }

                        if (has_top) {
                            f32 top_off = resolve_property(child->style(), "top", child_cb_height, root_font_size_);
                            child->content.y = top_off;
                        } else if (has_bottom) {
                            f32 bottom_off =
                                resolve_property(child->style(), "bottom", child_cb_height, root_font_size_);
                            child->content.y = child_cb_height - bottom_off - child->content.height;
                        }
                    }
                }
            }

            if (child_is_sticky) {
                f32 normal_x = child->content.x;
                f32 normal_y = child->content.y;

                if (child->style().get("top")) {
                    f32 sticky_top = resolve_property(child->style(), "top", viewport_height_, font_size);
                    child->content.y = std::max(normal_y, sticky_top);
                }
                if (child->style().get("left")) {
                    f32 sticky_left = resolve_property(child->style(), "left", viewport_width_, font_size);
                    child->content.x = std::max(normal_x, sticky_left);
                }
                if (child->style().get("bottom") && child->parent) {
                    f32 sticky_bottom = resolve_property(child->style(), "bottom", viewport_height_, font_size);
                    f32 container_bottom = child->parent->content.height;
                    f32 element_bottom = normal_y + child->content.height + child->padding.bottom +
                                         child->border.bottom + child->margin.bottom;
                    f32 min_y = container_bottom - element_bottom - sticky_bottom;
                    child->content.y = std::max(child->content.y, min_y);
                }
            }

            f32 child_fs = resolve_font_size(child->style(), font_size);

            if (child_is_absolute || child_is_fixed) {
                layout_absolute_pass(child.get(), nullptr, child_cb_width, child_cb_height, child_fs);
            } else {
                LayoutNode *next_cb = is_positioned(child->style()) ? child.get() : containing_block;
                layout_absolute_pass(child.get(), next_cb, child->content.width, child->content.height, child_fs);
            }
        }
    }

}  // namespace browser::css
