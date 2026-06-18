#include "painter.hpp"

#include "../../html/form_state.hpp"
#include "../canvas.hpp"
#include "../form_controls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace browser::render {

    Painter::Painter(TextRenderer *tr) : text_renderer_(tr) {}

    namespace {

        PaintCommand make_cmd(PaintCommand::Type type,
                              css::Rect rect,
                              Color color,
                              const std::string &text = "",
                              f32 font_size = 16,
                              ImageId image_id = 0,
                              const css::CSSGradient &gradient = {},
                              f32 radius = 0,
                              const css::Mat3x3 &transform = {},
                              f32 opacity = 1.0f,
                              u8 font_flags = 0) {
            PaintCommand cmd;
            cmd.type = type;
            cmd.rect = rect;
            cmd.color = color;
            cmd.text = text;
            cmd.font_size = font_size;
            cmd.font_flags = font_flags;
            cmd.image_id = image_id;
            cmd.gradient = gradient;
            cmd.radius = radius;
            cmd.transform = transform;
            cmd.opacity = opacity;
            return cmd;
        }

    }  // namespace

    void Painter::set_image_data(const std::unordered_map<std::string, std::shared_ptr<image::Image>> &images) {
        images_ = &images;
    }

    async::task<std::shared_ptr<DisplayList>> Painter::paint_async(css::LayoutNode *root) {
        co_await async::thread_pool_executor{};
        auto list = std::make_shared<DisplayList>();
        if (root)
            paint_node(*list, root, root->content.x, root->content.y, false);
        co_return list;
    }

    std::shared_ptr<DisplayList> Painter::build_display_list(css::LayoutNode *root, f32 offset_x, f32 offset_y) {
        auto list = std::make_shared<DisplayList>();
        if (root)
            paint_node(*list, root, offset_x, offset_y, true);
        return list;
    }

    namespace {

        bool paint_needs_own_layer(css::LayoutNode *node) {
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
            auto *pos = node->style().get("position");
            if (pos && pos->type == css::CSSValue::Type::KEYWORD) {
                if (pos->keyword == "fixed" || pos->keyword == "sticky")
                    return true;
            }
            if (node->has_transform)
                return true;
            auto *op = node->style().get("opacity");
            if (op && op->type == css::CSSValue::Type::NUMBER && op->number < 1.0f)
                return true;
            if (css::LayoutEngine::creates_stacking_context(node->style()))
                return true;
            auto *wc = node->style().get("will-change");
            if (wc && wc->type == css::CSSValue::Type::STRING &&
                wc->string_value.find("transform") != std::string::npos)
                return true;
            return false;
        }

    }  // namespace

    void Painter::paint_node(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy, bool skip_layer_children) const {
        if (!node)
            return;

        auto *vis = node->style().get("visibility");
        if (vis && vis->type == css::CSSValue::Type::KEYWORD && vis->keyword == "hidden") {
            // Push clip to the hidden node's padding box so children are clipped
            f32 px = ox - node->padding.left;
            f32 py = oy - node->padding.top;
            f32 pw = node->content.width + node->padding.left + node->padding.right;
            f32 ph = node->content.height + node->padding.top + node->padding.bottom;
            list.push(make_cmd(PaintCommand::Type::PUSH_CLIP, {px, py, pw, ph}, Color::TRANSPARENT));
            for (auto &child : node->children) {
                if (skip_layer_children && paint_needs_own_layer(child.get()))
                    continue;
                paint_node(list,
                           child.get(),
                           ox + child->content.x + child->scroll_offset_x,
                           oy + child->content.y + child->scroll_offset_y,
                           skip_layer_children);
            }
            list.push(make_cmd(PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT));
            return;
        }

        auto *opacity_val = node->style().get("opacity");
        f32 opacity = 1.0f;
        if (opacity_val && opacity_val->type == css::CSSValue::Type::NUMBER) {
            opacity = std::max(0.0f, std::min(1.0f, opacity_val->number));
        }
        node->opacity = opacity;

        bool needs_opacity_layer = opacity < 1.0f;
        if (needs_opacity_layer) {
            list.push(make_cmd(PaintCommand::Type::PUSH_OPACITY, {}, Color::TRANSPARENT, "", 0, 0, {}, 0, {}, opacity));
        }

        if (node->has_transform) {
            list.push(make_cmd(
                PaintCommand::Type::PUSH_TRANSFORM, {}, Color::TRANSPARENT, "", 0, 0, {}, 0, node->transform_matrix));
        }

        // Check if this is a form control
        bool is_form_control = false;
        html::Element *el = nullptr;
        if (!node->is_text()) {
            html::Node *n = node->node();
            if (n && n->type == html::NodeType::ELEMENT) {
                el = static_cast<html::Element *>(n);
                is_form_control = (el->tag_name == "input" || el->tag_name == "button" || el->tag_name == "select" ||
                                   el->tag_name == "textarea");
            }
        }

        if (is_form_control && el) {
            std::string type = el->get_attribute("type");
            std::string value = html::g_form_state.get_value(el);
            bool focused = (html::g_form_state.focused_element == el);
            bool hovered = (html::g_form_state.hovered_element == el);
            u32 caret = focused ? html::g_form_state.caret_position : 0;
            f32 fx = ox - node->padding.left - node->border.left;
            f32 fy = oy - node->padding.top - node->border.top;
            f32 fw =
                node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
            f32 fh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                     node->border.bottom;

            if (el->tag_name == "input" && (type.empty() || type == "text")) {
                form_controls::paint_text_input(list, fx, fy, fw, fh, value, caret, focused);
            } else if (el->tag_name == "input" && type == "checkbox") {
                bool checked = html::g_form_state.is_checked(el);
                form_controls::paint_checkbox(list, fx, fy, 13, checked);
            } else if (el->tag_name == "input" && type == "radio") {
                bool checked = html::g_form_state.is_checked(el);
                form_controls::paint_radio(list, fx, fy, 13, checked);
            } else if (el->tag_name == "button" || (el->tag_name == "input" && type == "submit")) {
                std::string label = value.empty() ? (el->tag_name == "button" ? "Button" : "Submit") : value;
                form_controls::paint_button(list, fx, fy, fw, fh, label, hovered, focused);
            } else if (el->tag_name == "select") {
                form_controls::paint_select(list, fx, fy, fw, fh, value, false);
            } else if (el->tag_name == "textarea") {
                form_controls::paint_textarea(list, fx, fy, fw, fh, value, 0, caret, focused);
            }
        } else {
            paint_background(list, node, ox, oy);
            paint_shadow(list, node, ox, oy);
            paint_border(list, node, ox, oy);
            paint_outline(list, node, ox, oy);
            paint_image(list, node, ox, oy);
            paint_canvas(list, node, ox, oy);
        }

        bool has_clip = false;
        auto *overflow = node->style().get("overflow");
        if (overflow && overflow->type == css::CSSValue::Type::KEYWORD) {
            has_clip = (overflow->keyword == "hidden" || overflow->keyword == "scroll" || overflow->keyword == "auto");
        }
        if (!has_clip) {
            auto *ox_val = node->style().get("overflow-x");
            if (ox_val && ox_val->type == css::CSSValue::Type::KEYWORD) {
                has_clip = (ox_val->keyword == "hidden" || ox_val->keyword == "scroll" || ox_val->keyword == "auto");
            }
        }
        if (!has_clip) {
            auto *oy_val = node->style().get("overflow-y");
            if (oy_val && oy_val->type == css::CSSValue::Type::KEYWORD) {
                has_clip = (oy_val->keyword == "hidden" || oy_val->keyword == "scroll" || oy_val->keyword == "auto");
            }
        }

        if (has_clip) {
            f32 px = ox - node->padding.left;
            f32 py = oy - node->padding.top;
            f32 pw = node->content.width + node->padding.left + node->padding.right;
            f32 ph = node->content.height + node->padding.top + node->padding.bottom;
            list.push(make_cmd(PaintCommand::Type::PUSH_CLIP, {px, py, pw, ph}, Color::TRANSPARENT));
        }

        if (node->is_text()) {
            paint_text(list, node, ox, oy);
        }

        std::vector<css::LayoutNode *> negative_z;
        std::vector<css::LayoutNode *> normal_flow;
        std::vector<css::LayoutNode *> positive_z;

        for (auto &child : node->children) {
            if (skip_layer_children && paint_needs_own_layer(child.get()))
                continue;
            f32 zi = css::LayoutEngine::get_z_index(child->style());
            bool creates_stack = css::LayoutEngine::creates_stacking_context(child->style());

            if (creates_stack) {
                if (zi < 0) {
                    negative_z.push_back(child.get());
                } else {
                    positive_z.push_back(child.get());
                }
            } else {
                normal_flow.push_back(child.get());
            }
        }

        auto sort_by_z = [](std::vector<css::LayoutNode *> &v) {
            for (size_t i = 1; i < v.size(); i++) {
                css::LayoutNode *key = v[i];
                f32 key_z = css::LayoutEngine::get_z_index(key->style());
                i32 j = static_cast<i32>(i) - 1;
                while (j >= 0 && css::LayoutEngine::get_z_index(v[static_cast<size_t>(j)]->style()) > key_z) {
                    v[static_cast<size_t>(j + 1)] = v[static_cast<size_t>(j)];
                    j--;
                }
                v[static_cast<size_t>(j + 1)] = key;
            }
        };
        sort_by_z(negative_z);
        sort_by_z(positive_z);

        for (auto *child : negative_z) {
            paint_node(list,
                       child,
                       ox + child->content.x + child->scroll_offset_x,
                       oy + child->content.y + child->scroll_offset_y,
                       skip_layer_children);
        }

        for (auto *child : normal_flow) {
            paint_node(list,
                       child,
                       ox + child->content.x + child->scroll_offset_x,
                       oy + child->content.y + child->scroll_offset_y,
                       skip_layer_children);
        }

        for (auto *child : positive_z) {
            paint_node(list,
                       child,
                       ox + child->content.x + child->scroll_offset_x,
                       oy + child->content.y + child->scroll_offset_y,
                       skip_layer_children);
        }

        if (has_clip) {
            list.push(make_cmd(PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT));
        }

        if (node->has_transform) {
            list.push(make_cmd(PaintCommand::Type::POP_TRANSFORM, {}, Color::TRANSPARENT));
        }

        if (needs_opacity_layer) {
            list.push(make_cmd(PaintCommand::Type::POP_OPACITY, {}, Color::TRANSPARENT));
        }
    }

    void Painter::paint_background(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *bg_img = node->style().get("background-image");
        if (bg_img && bg_img->type == css::CSSValue::Type::GRADIENT) {
            f32 bx = ox - node->padding.left;
            f32 by = oy - node->padding.top;
            f32 bw =
                node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
            f32 bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                     node->border.bottom;

            list.push(make_cmd(
                PaintCommand::Type::DRAW_GRADIENT, {bx, by, bw, bh}, Color::TRANSPARENT, "", 0, 0, bg_img->gradient));
            return;
        }

        Color bg = resolve_color(node->style(), "background-color", Color::TRANSPARENT);
        if (bg.a == 0.0f)
            return;

        f32 bx = ox - node->padding.left;
        f32 by = oy - node->padding.top;
        f32 bw =
            node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
        f32 bh =
            node->content.height + node->padding.top + node->padding.bottom + node->border.top + node->border.bottom;

        auto *br = node->style().get("border-radius");
        f32 radius = 0;
        if (br && br->type == css::CSSValue::Type::LENGTH) {
            radius = br->length.value;
        }

        if (radius > 0) {
            list.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT, {bx, by, bw, bh}, bg, "", 0, 0, {}, radius));
        } else {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, bg));
        }
    }

    static f32 parse_shadow_len(const std::string &s, size_t &pos) {
        while (pos < s.size() && s[pos] == ' ') pos++;
        if (pos >= s.size())
            return 0;
        char *end = nullptr;
        f32 val = std::strtof(s.c_str() + pos, &end);
        if (end && end != s.c_str() + pos) {
            pos = static_cast<size_t>(end - s.c_str());
            // Skip unit suffix (px, em, etc.)
            while (pos < s.size() &&
                   (s[pos] == 'p' || s[pos] == 'x' || s[pos] == 'e' || s[pos] == 'm' || s[pos] == 'r' ||
                    s[pos] == 'v' || s[pos] == 'h' || s[pos] == 'w' || s[pos] == '%' || s[pos] == 'c' || s[pos] == 't'))
                pos++;
            return val;
        }
        return 0;
    }

    void Painter::paint_shadow(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *bs = node->style().get("box-shadow");
        if (bs && bs->type == css::CSSValue::Type::STRING && !bs->string_value.empty()) {
            std::string s = bs->string_value;
            // Ignore inset keyword
            size_t pos = 0;
            while (pos < s.size() && s[pos] == ' ') pos++;
            if (s.substr(pos, 5) == "inset") {
                pos += 5;
                while (pos < s.size() && s[pos] == ' ') pos++;
            }

            f32 off_x = parse_shadow_len(s, pos);
            f32 off_y = parse_shadow_len(s, pos);
            f32 blur = parse_shadow_len(s, pos);
            // Skip spread radius if present
            parse_shadow_len(s, pos);

            Color shadow_color = {0, 0, 0, 0.5f};
            while (pos < s.size() && s[pos] == ' ') pos++;
            if (pos < s.size()) {
                std::string color_str = s.substr(pos);
                // Try named color first
                auto css_c = css::Color::from_name(color_str);
                if (css_c.a != 0 || color_str == "transparent") {
                    shadow_color = css_to_render_color(css_c);
                } else {
                    // Try rgba(r,g,b,a) or rgb(r,g,b)
                    // (simplified: actual CSS color parsing is done by the cascade layer)
                    // Fallback: just use default shadow color
                }
            }

            f32 bx = ox - node->padding.left + off_x;
            f32 by = oy - node->padding.top + off_y;
            f32 bw =
                node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
            f32 bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                     node->border.bottom;

            if (blur > 0) {
                list.push(make_cmd(PaintCommand::Type::DRAW_SHADOW,
                                   {bx - blur, by - blur, bw + 2 * blur, bh + 2 * blur},
                                   shadow_color,
                                   "",
                                   0,
                                   0,
                                   {},
                                   blur));
            } else {
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, shadow_color));
            }
        }
    }

    void Painter::paint_border(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto &b = node->border;
        if (b.top == 0.0f && b.right == 0.0f && b.bottom == 0.0f && b.left == 0.0f)
            return;

        Color border_color = resolve_color_fallback(node->style(), {"border-top-color", "border-color"}, Color::BLACK);
        Color border_right =
            resolve_color_fallback(node->style(), {"border-right-color", "border-color"}, Color::BLACK);
        Color border_bottom =
            resolve_color_fallback(node->style(), {"border-bottom-color", "border-color"}, Color::BLACK);
        Color border_left = resolve_color_fallback(node->style(), {"border-left-color", "border-color"}, Color::BLACK);

        f32 bx = ox - node->padding.left - b.left;
        f32 by = oy - node->padding.top - b.top;
        f32 bw = node->content.width + node->padding.left + node->padding.right + b.left + b.right;
        f32 bh = node->content.height + node->padding.top + node->padding.bottom + b.top + b.bottom;

        if (b.top > 0.0f) {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, b.top}, border_color));
        }
        if (b.right > 0.0f) {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - b.right, by, b.right, bh}, border_right));
        }
        if (b.bottom > 0.0f) {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - b.bottom, bw, b.bottom}, border_bottom));
        }
        if (b.left > 0.0f) {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, b.left, bh}, border_left));
        }
    }

    void Painter::paint_text(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        Color text_color = resolve_color(node->style(), "color", Color::BLACK);
        f32 font_size = resolve_font_size(node->style());
        f32 descender_pad = std::ceil(font_size * 0.25f);

        std::string font_family;
        auto *ff_val = node->style().get("font-family");
        if (ff_val && ff_val->type == css::CSSValue::Type::STRING) {
            font_family = ff_val->string_value;
        } else if (ff_val && ff_val->type == css::CSSValue::Type::KEYWORD) {
            font_family = ff_val->keyword;
        }

        u8 font_flags = 0;
        auto *fw = node->style().get("font-weight");
        if (fw && fw->type == css::CSSValue::Type::KEYWORD) {
            if (fw->keyword == "bold" || fw->keyword == "bolder")
                font_flags |= 1;
        }
        if (fw && fw->type == css::CSSValue::Type::NUMBER) {
            if (fw->number >= 700)
                font_flags |= 1;
        }
        auto *fs = node->style().get("font-style");
        if (fs && fs->type == css::CSSValue::Type::KEYWORD && fs->keyword == "italic")
            font_flags |= 2;

        // Check for text-decoration
        bool has_underline = false;
        auto *dec_val = node->style().get("text-decoration");
        if (dec_val && dec_val->type == css::CSSValue::Type::KEYWORD) {
            has_underline = (dec_val->keyword.find("underline") != std::string::npos);
        }

        if (!node->text_lines.empty()) {
            for (auto &li : node->text_lines) {
                css::Rect line_rect = {ox, oy + li.y, node->content.width, font_size + descender_pad};
                auto tc = make_cmd(PaintCommand::Type::DRAW_TEXT,
                                   line_rect,
                                   text_color,
                                   li.text,
                                   font_size,
                                   0,
                                   {},
                                   0,
                                   {},
                                   1.0f,
                                   font_flags);
                tc.font_family = font_family;
                list.push(tc);
                if (has_underline) {
                    f32 underline_y = oy + li.y + font_size + 1.0f;
                    f32 thickness = std::max(1.0f, font_size / 14.0f);
                    list.push(make_cmd(
                        PaintCommand::Type::FILL_RECT, {ox, underline_y, node->content.width, thickness}, text_color));
                }
            }
        } else {
            // No wrapping info — single line fallback
            auto tc = make_cmd(PaintCommand::Type::DRAW_TEXT,
                               {ox, oy, node->content.width, node->content.height + descender_pad},
                               text_color,
                               node->text(),
                               font_size,
                               0,
                               {},
                               0,
                               {},
                               1.0f,
                               font_flags);
            tc.font_family = font_family;
            list.push(tc);
            if (has_underline) {
                f32 underline_y = oy + font_size + 1.0f;
                f32 thickness = std::max(1.0f, font_size / 14.0f);
                list.push(make_cmd(
                    PaintCommand::Type::FILL_RECT, {ox, underline_y, node->content.width, thickness}, text_color));
            }
        }
    }

    void Painter::paint_image(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        if (node->is_text())
            return;
        html::Node *n = node->node();
        if (!n || n->type != html::NodeType::ELEMENT)
            return;
        auto *el = static_cast<html::Element *>(n);
        if (el->tag_name != "img")
            return;

        std::string src = el->get_attribute("src");
        if (src.empty() || !images_)
            return;

        auto it = images_->find(src);
        if (it == images_->end() || !it->second)
            return;

        auto *img = it->second.get();
        ImageId id = reinterpret_cast<ImageId>(img);
        f32 bx = ox;
        f32 by = oy;
        f32 bw = node->content.width > 0 ? node->content.width : static_cast<f32>(img->width);
        f32 bh = node->content.height > 0 ? node->content.height : static_cast<f32>(img->height);

        list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE, {bx, by, bw, bh}, Color::WHITE, "", 0, id));
    }

    void Painter::paint_canvas(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        if (node->is_text())
            return;
        html::Node *n = node->node();
        if (!n || n->type != html::NodeType::ELEMENT)
            return;
        auto *el = static_cast<html::Element *>(n);
        if (el->tag_name != "canvas")
            return;

        auto it = g_canvas_registry.find(el);
        if (it == g_canvas_registry.end() || !it->second)
            return;

        auto *canvas = it->second.get();
        if (canvas->width() == 0 || canvas->height() == 0)
            return;

        f32 bx = ox;
        f32 by = oy;
        f32 bw = node->content.width > 0 ? node->content.width : static_cast<f32>(canvas->width());
        f32 bh = node->content.height > 0 ? node->content.height : static_cast<f32>(canvas->height());

        // Copy pixel data into the command for thread safety
        PaintCommand cmd;
        cmd.type = PaintCommand::Type::DRAW_CANVAS;
        cmd.rect = {bx, by, bw, bh};
        cmd.color = Color::WHITE;
        cmd.canvas_data_w = canvas->width();
        cmd.canvas_data_h = canvas->height();
        {
            const u8 *pixels = canvas->pixels();
            u32 count = cmd.canvas_data_w * cmd.canvas_data_h * 4;
            cmd.canvas_pixels.assign(pixels, pixels + count);
        }
        list.push(cmd);
    }

    void Painter::paint_outline(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *ow = node->style().get("outline-width");
        f32 outline_width = 0;
        if (ow && ow->type == css::CSSValue::Type::LENGTH) {
            outline_width = ow->length.value;
        }
        if (outline_width <= 0) {
            auto *outline = node->style().get("outline");
            if (outline && outline->type == css::CSSValue::Type::STRING) {
                std::string s = outline->string_value;
                char *end = nullptr;
                f32 w = std::strtof(s.c_str(), &end);
                if (end && end != s.c_str() && w > 0) {
                    outline_width = w;
                }
            }
        }
        if (outline_width <= 0)
            return;

        auto *os = node->style().get("outline-style");
        if (os && os->type == css::CSSValue::Type::KEYWORD && os->keyword == "none")
            return;

        f32 outline_offset = 0;
        auto *oo = node->style().get("outline-offset");
        if (oo && oo->type == css::CSSValue::Type::LENGTH) {
            outline_offset = oo->length.value;
        }

        Color outline_color = resolve_color_fallback(node->style(), {"outline-color", "outline"}, Color::BLACK);

        f32 bx = ox - node->padding.left - node->border.left - outline_offset - outline_width;
        f32 by = oy - node->padding.top - node->border.top - outline_offset - outline_width;
        f32 bw = node->content.width + node->padding.left + node->padding.right + node->border.left +
                 node->border.right + 2 * outline_offset + 2 * outline_width;
        f32 bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                 node->border.bottom + 2 * outline_offset + 2 * outline_width;

        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, outline_width}, outline_color));
        list.push(
            make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - outline_width, by, outline_width, bh}, outline_color));
        list.push(
            make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - outline_width, bw, outline_width}, outline_color));
        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, outline_width, bh}, outline_color));
    }

    Color Painter::resolve_color(const css::ComputedStyle &style,
                                 const std::string &prop,
                                 const Color &fallback) const {
        auto *v = style.get(prop);
        if (!v)
            return fallback;

        if (v->type == css::CSSValue::Type::COLOR) {
            return css_to_render_color(v->color);
        }

        if (v->type == css::CSSValue::Type::KEYWORD) {
            if (v->keyword == "inherit" && style.parent) {
                return resolve_color(*style.parent, prop, fallback);
            }
            auto css_c = css::Color::from_name(v->keyword);
            return css_to_render_color(css_c);
        }

        return fallback;
    }

    Color Painter::resolve_color_fallback(const css::ComputedStyle &style,
                                          const std::vector<std::string> &props,
                                          const Color &fallback) const {
        for (const auto &prop : props) {
            if (style.get(prop)) {
                return resolve_color(style, prop, fallback);
            }
        }
        return fallback;
    }

    f32 Painter::resolve_font_size(const css::ComputedStyle &style) const {
        auto *v = style.get("font-size");
        if (!v)
            return 16.0f;

        if (v->type == css::CSSValue::Type::LENGTH) {
            switch (v->length.unit) {
                case css::Length::Unit::PX:
                    return v->length.value;
                case css::Length::Unit::EM:
                    return v->length.value * 16.0f;
                case css::Length::Unit::REM:
                    return v->length.value * 16.0f;
                default:
                    return 16.0f;
            }
        }

        if (v->type == css::CSSValue::Type::KEYWORD) {
            if (v->keyword == "small")
                return 13.0f;
            if (v->keyword == "medium")
                return 16.0f;
            if (v->keyword == "large")
                return 18.0f;
            if (v->keyword == "x-large")
                return 24.0f;
            if (v->keyword == "xx-large")
                return 32.0f;
        }

        return 16.0f;
    }

}  // namespace browser::render
