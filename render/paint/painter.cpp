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
        if (vis && vis->type == css::CSSValue::Type::KEYWORD &&
            (vis->keyword == "hidden" || vis->keyword == "collapse")) {
            // visibility: hidden/collapse — element is invisible but still takes up space
            // Don't paint the element or its children, just return
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

        // Check for filter property
        bool has_filter = false;
        auto *filter_val = node->style().get("filter");
        if (filter_val && filter_val->type == css::CSSValue::Type::FILTER_LIST && !filter_val->filters.empty()) {
            has_filter = true;
            PaintCommand push_cmd;
            push_cmd.type = PaintCommand::Type::PUSH_FILTER;
            push_cmd.filters = filter_val->filters;
            // Set the element's border box rect for FBO sizing (executor adds blur padding)
            f32 bx = ox - node->padding.left - node->border.left;
            f32 by = oy - node->padding.top - node->border.top;
            f32 bw = node->content.width + node->padding.left + node->padding.right +
                     node->border.left + node->border.right;
            f32 bh = node->content.height + node->padding.top + node->padding.bottom +
                     node->border.top + node->border.bottom;
            push_cmd.rect = {bx, by, bw, bh};
            list.push(push_cmd);
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
            std::string placeholder = el->get_attribute("placeholder");
            bool disabled = el->has_attribute("disabled");
            // Resolve accent-color and caret-color from computed style (inherited)
            Color accent = {0.2f, 0.4f, 0.9f, 1};  // default blue
            Color caret_col = {0, 0, 0, 1};           // default black
            auto *acc = node->style().get("accent-color");
            if (acc && acc->type == css::CSSValue::Type::COLOR) {
                accent = {static_cast<f32>(acc->color.r) / 255.0f,
                          static_cast<f32>(acc->color.g) / 255.0f,
                          static_cast<f32>(acc->color.b) / 255.0f, 1.0f};
            } else if (acc && acc->type == css::CSSValue::Type::KEYWORD) {
                auto named = css::Color::from_name(acc->keyword);
                if (named.a != 0 || acc->keyword == "transparent")
                    accent = {static_cast<f32>(named.r) / 255.0f, static_cast<f32>(named.g) / 255.0f,
                              static_cast<f32>(named.b) / 255.0f, static_cast<f32>(named.a) / 255.0f};
            }
            auto *caret_css = node->style().get("caret-color");
            if (caret_css && caret_css->type == css::CSSValue::Type::COLOR) {
                caret_col = {static_cast<f32>(caret_css->color.r) / 255.0f,
                             static_cast<f32>(caret_css->color.g) / 255.0f,
                             static_cast<f32>(caret_css->color.b) / 255.0f, 1.0f};
            } else if (caret_css && caret_css->type == css::CSSValue::Type::KEYWORD) {
                auto named = css::Color::from_name(caret_css->keyword);
                if (named.a != 0 || caret_css->keyword == "transparent")
                    caret_col = {static_cast<f32>(named.r) / 255.0f, static_cast<f32>(named.g) / 255.0f,
                                 static_cast<f32>(named.b) / 255.0f, static_cast<f32>(named.a) / 255.0f};
            }
            bool focused = (html::g_form_state.focused_element == el);
            bool hovered = (html::g_form_state.hovered_element == el);
            u32 caret = focused ? html::g_form_state.caret_position : 0;
            f32 fx = ox - node->padding.left - node->border.left;
            f32 fy = oy - node->padding.top - node->border.top;
            f32 fw =
                node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
            f32 fh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                     node->border.bottom;

            if (el->tag_name == "input" && type == "hidden") {
                // Hidden inputs are not rendered
            } else if (el->tag_name == "input" && (type.empty() || type == "text" || type == "email" || type == "search" || type == "url")) {
                form_controls::paint_text_input(list, fx, fy, fw, fh, value, placeholder, caret, focused, disabled, caret_col);
            } else if (el->tag_name == "input" && type == "number") {
                f32 spin_active = 0;
                form_controls::paint_number_input(list, fx, fy, fw, fh, value, placeholder, caret, focused, spin_active, disabled, caret_col);
            } else if (el->tag_name == "input" && type == "password") {
                std::string display(value.size(), '*');
                form_controls::paint_text_input(list, fx, fy, fw, fh, display, placeholder, caret, focused, disabled, caret_col);
            } else if (el->tag_name == "input" && type == "checkbox") {
                bool checked = html::g_form_state.is_checked(el);
                form_controls::paint_checkbox(list, fx, fy, 13, checked, accent);
            } else if (el->tag_name == "input" && type == "radio") {
                bool checked = html::g_form_state.is_checked(el);
                form_controls::paint_radio(list, fx, fy, 13, checked, accent);
            } else if (el->tag_name == "button" || (el->tag_name == "input" && (type == "submit" || type == "reset"))) {
                std::string label = value.empty() ? (type == "reset" ? "Reset" : (el->tag_name == "button" ? "Button" : "Submit")) : value;
                form_controls::paint_button(list, fx, fy, fw, fh, label, hovered, focused);
            } else if (el->tag_name == "select") {
                bool is_open = (html::g_form_state.open_select == el);
                bool is_multiple = el->has_attribute("multiple");
                int sel_idx = html::g_form_state.get_selected_index(el);
                form_controls::paint_select(list, fx, fy, fw, fh, value, is_open);

                if (is_multiple) {
                    // Multiple select: render as a list box, always showing all options
                    int opt_count = 0;
                    std::function<int(html::Node*)> count_opts = [&](html::Node *parent) -> int {
                        int n = 0;
                        for (auto &c : parent->children) {
                            if (c->type == html::NodeType::ELEMENT) {
                                auto *ch = static_cast<html::Element*>(c.get());
                                if (ch->tag_name == "option") n++;
                                else if (ch->tag_name == "optgroup") n += count_opts(ch);
                            }
                        }
                        return n;
                    };
                    opt_count = count_opts(el);
                    if (opt_count > 0) {
                        f32 list_h = std::min(static_cast<f32>(opt_count) * 20.0f, 200.0f);
                        f32 opt_y = fy + fh;
                        f32 opt_w = fw;
                        // Border around list
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y, opt_w, 1}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y + list_h - 1, opt_w, 1}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y, 1, list_h}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx + opt_w - 1, opt_y, 1, list_h}, Color{0.5f, 0.5f, 0.5f, 1}));

                        // Store dropdown rect for hit testing
                        html::g_form_state.select_dropdown_rect = {fx, opt_y, opt_w, list_h};

                        int idx = 0;
                        f32 yy = 0;
                        std::function<void(html::Node*, f32&)> render_mult = [&](html::Node *parent, f32 &ypos) {
                            for (auto &c : parent->children) {
                                if (c->type != html::NodeType::ELEMENT) continue;
                                auto *ch = static_cast<html::Element*>(c.get());
                                if (ch->tag_name == "option") {
                                    std::string opt_text = ch->get_attribute("label");
                                    if (opt_text.empty()) {
                                        for (auto &tc : ch->children)
                                            if (tc->type == html::NodeType::TEXT)
                                                opt_text += static_cast<html::Text*>(tc.get())->data;
                                    }
                                    if (opt_text.empty()) opt_text = ch->get_attribute("value");
                                    Color opt_color = (idx == sel_idx) ? Color{0.2f, 0.4f, 0.9f, 1} : Color{0, 0, 0, 1};
                                    if (idx == sel_idx) {
                                        list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                                            {fx + 1, opt_y + ypos, opt_w - 2, 20.0f}, Color{0.8f, 0.85f, 1.0f, 1}));
                                    }
                                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                                        {fx + 4, opt_y + ypos + 2, opt_w - 8, 18.0f}, opt_color, opt_text, 14));
                                    idx++;
                                    ypos += 20.0f;
                                } else if (ch->tag_name == "optgroup") {
                                    std::string grp_label = ch->get_attribute("label");
                                    list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                                        {fx + 1, opt_y + ypos, opt_w - 2, 20.0f}, Color{0.94f, 0.94f, 0.94f, 1}));
                                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                                        {fx + 8, opt_y + ypos + 2, opt_w - 12, 18.0f}, Color{0.35f, 0.35f, 0.35f, 1}, grp_label, 13));
                                    ypos += 20.0f;
                                    f32 saved_x = fx;
                                    fx += 14.0f;
                                    opt_w -= 14.0f;
                                    render_mult(ch, ypos);
                                    fx = saved_x;
                                    opt_w += 14.0f;
                                }
                            }
                        };
                        render_mult(el, yy);
                    }
                } else if (is_open) {
                    // Count visible options (including those in optgroups)
                    std::function<int(html::Node*)> count_options = [&](html::Node *parent) -> int {
                        int n = 0;
                        for (auto &c : parent->children) {
                            if (c->type == html::NodeType::ELEMENT) {
                                auto *ch = static_cast<html::Element*>(c.get());
                                if (ch->tag_name == "option") n++;
                                else if (ch->tag_name == "optgroup") n += count_options(ch);
                            }
                        }
                        return n;
                    };
                    int opt_count = count_options(el);
                    // Render dropdown options
                    f32 opt_y = fy + fh;
                    f32 opt_w = fw;
                    if (opt_count > 0) {
                        f32 opt_h = std::min(static_cast<f32>(opt_count) * 20.0f, 200.0f);
                        // Store dropdown rect for hit testing
                        html::g_form_state.select_dropdown_rect = {fx, opt_y, opt_w, opt_h};
                        // Dropdown background
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y, opt_w, opt_h}, Color{1, 1, 1, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y, opt_w, 1}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y + opt_h - 1, opt_w, 1}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx, opt_y, 1, opt_h}, Color{0.5f, 0.5f, 0.5f, 1}));
                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx + opt_w - 1, opt_y, 1, opt_h}, Color{0.5f, 0.5f, 0.5f, 1}));

                        int idx = 0;
                        std::function<void(html::Node*, f32&)> render_opts = [&](html::Node *parent, f32 &ypos) {
                            for (auto &child : parent->children) {
                                if (child->type != html::NodeType::ELEMENT) continue;
                                auto *opt = static_cast<html::Element*>(child.get());
                                if (opt->tag_name == "option") {
                                    std::string opt_text = opt->get_attribute("label");
                                    if (opt_text.empty()) {
                                        for (auto &tc : opt->children) {
                                            if (tc->type == html::NodeType::TEXT)
                                                opt_text += static_cast<html::Text*>(tc.get())->data;
                                        }
                                    }
                                    if (opt_text.empty()) opt_text = opt->get_attribute("value");
                                    Color opt_color = (idx == sel_idx) ? Color{0.2f, 0.4f, 0.9f, 1} : Color{0, 0, 0, 1};
                                    if (idx == sel_idx) {
                                        list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx + 1, opt_y + ypos, opt_w - 2, 20.0f}, Color{0.8f, 0.85f, 1.0f, 1}));
                                    }
                                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {fx + 4, opt_y + ypos + 2, opt_w - 8, 18.0f}, opt_color, opt_text, 14));
                                    idx++;
                                    ypos += 20.0f;
                                } else if (opt->tag_name == "optgroup") {
                                    // Render optgroup label
                                    std::string grp_label = opt->get_attribute("label");
                                    list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fx + 1, opt_y + ypos, opt_w - 2, 20.0f}, Color{0.94f, 0.94f, 0.94f, 1}));
                                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {fx + 8, opt_y + ypos + 2, opt_w - 12, 18.0f}, Color{0.35f, 0.35f, 0.35f, 1}, grp_label, 13));
                                    ypos += 20.0f;
                                    // Render children with indent
                                    f32 saved_x = fx;
                                    fx += 14.0f;
                                    opt_w -= 14.0f;
                                    render_opts(opt, ypos);
                                    fx = saved_x;
                                    opt_w += 14.0f;
                                }
                            }
                        };
                        f32 yy = 0;
                        render_opts(el, yy);
                    }
                } else {
                    html::g_form_state.select_dropdown_rect = {0, 0, 0, 0};
                }
            } else if (el->tag_name == "textarea") {
                // Compute line and column from caret position for multi-line support
                u32 line = 0, col = caret;
                size_t last_newline = 0;
                for (size_t i = 0; i < value.size() && i < caret; i++) {
                    if (value[i] == '\n') {
                        line++;
                        last_newline = i + 1;
                    }
                }
                col = static_cast<u32>(caret - last_newline);
                form_controls::paint_textarea(list, fx, fy, fw, fh, value, line, col, focused);
            } else if (el->tag_name == "input" && type == "file") {
                form_controls::paint_file_input(list, fx, fy, fw, fh, value, focused);
            } else if (el->tag_name == "input" && type == "range") {
                f32 min_val = 0, max_val = 100;
                std::string min_str = el->get_attribute("min");
                std::string max_str = el->get_attribute("max");
                if (!min_str.empty()) min_val = std::strtof(min_str.c_str(), nullptr);
                if (!max_str.empty()) max_val = std::strtof(max_str.c_str(), nullptr);
                f32 cur = std::strtof(value.c_str(), nullptr);
                form_controls::paint_range(list, fx, fy, fw, fh, cur, min_val, max_val, focused, accent);
            } else if (el->tag_name == "input" && type == "color") {
                form_controls::paint_color_input(list, fx, fy, fw, fh, value, focused);
            } else if (el->tag_name == "progress") {
                f32 max_val = 1.0f;
                std::string max_str = el->get_attribute("max");
                if (!max_str.empty()) max_val = std::strtof(max_str.c_str(), nullptr);
                f32 cur = std::strtof(value.c_str(), nullptr);
                form_controls::paint_progress(list, fx, fy, fw, fh, cur, max_val, accent);
            } else if (el->tag_name == "fieldset") {
                // Paint fieldset border with legend gap
                // Find legend element
                std::string legend_text;
                f32 legend_w = 0;
                for (auto &child : el->children) {
                    if (child->type == html::NodeType::ELEMENT) {
                        auto *ch_el = static_cast<html::Element *>(child.get());
                        if (ch_el->tag_name == "legend") {
                            for (auto &tc : ch_el->children) {
                                if (tc->type == html::NodeType::TEXT)
                                    legend_text += static_cast<html::Text *>(tc.get())->data;
                            }
                            legend_w = static_cast<f32>(legend_text.size()) * 7.0f;
                            break;
                        }
                    }
                }
                // Fieldset border (inset style, with gap for legend)
                f32 field_pad = 8.0f;
                f32 fbx = fx + field_pad;
                f32 fby = fy + field_pad;
                f32 fbw = fw - 2 * field_pad;
                f32 fbh = fh - 2 * field_pad;
                // Top border — left part (before legend), right part (after legend)
                f32 legend_gap_start = fbx + 10;
                f32 legend_gap_end = legend_gap_start + legend_w + 6;
                if (legend_text.empty()) {
                    list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fbx, fby, fbw, 1}, {0.5f, 0.5f, 0.5f, 1}));
                } else {
                    f32 left_w = legend_gap_start - fbx;
                    if (left_w > 0) list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fbx, fby, left_w, 1}, {0.5f, 0.5f, 0.5f, 1}));
                    f32 right_start = legend_gap_end;
                    f32 right_w = fbx + fbw - right_start;
                    if (right_w > 0) list.push(make_cmd(PaintCommand::Type::FILL_RECT, {right_start, fby, right_w, 1}, {0.5f, 0.5f, 0.5f, 1}));
                    // Legend text
                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                        {legend_gap_start + 2, fby - 6, legend_w + 2, 14.0f}, {0, 0, 0, 1}, legend_text, 14));
                }
                // Bottom border
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fbx, fby + fbh - 1, fbw, 1}, {0.5f, 0.5f, 0.5f, 1}));
                // Left border
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fbx, fby, 1, fbh}, {0.5f, 0.5f, 0.5f, 1}));
                // Right border
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {fbx + fbw - 1, fby, 1, fbh}, {0.5f, 0.5f, 0.5f, 1}));
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

            // <details> without "open": only show <summary> children
            if (!node->is_text() && node->node() && node->node()->type == html::NodeType::ELEMENT) {
                auto *parent_el = static_cast<html::Element *>(node->node());
                if (parent_el->tag_name == "details" && !parent_el->has_attribute("open")) {
                    bool is_summary = false;
                    if (!child->is_text() && child->node() && child->node()->type == html::NodeType::ELEMENT) {
                        auto *child_el = static_cast<html::Element *>(child->node());
                        if (child_el->tag_name == "summary")
                            is_summary = true;
                    }
                    if (!is_summary)
                        continue;
                }
            }

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

        if (has_filter) {
            list.push(make_cmd(PaintCommand::Type::POP_FILTER, {}, Color::TRANSPARENT));
        }

        if (needs_opacity_layer) {
            list.push(make_cmd(PaintCommand::Type::POP_OPACITY, {}, Color::TRANSPARENT));
        }
    }

    void Painter::paint_background(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *bg_img = node->style().get("background-image");
        f32 bx = ox - node->padding.left;
        f32 by = oy - node->padding.top;
        f32 bw =
            node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
        f32 bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                 node->border.bottom;

        if (bg_img && bg_img->type == css::CSSValue::Type::GRADIENT) {
            list.push(make_cmd(
                PaintCommand::Type::DRAW_GRADIENT, {bx, by, bw, bh}, Color::TRANSPARENT, "", 0, 0, bg_img->gradient));
            return;
        }

        // Handle background-image from shorthand (KEYWORD with url() or gradient string)
        if (bg_img && (bg_img->type == css::CSSValue::Type::KEYWORD || bg_img->type == css::CSSValue::Type::STRING ||
                       bg_img->type == css::CSSValue::Type::URL)) {
            std::string val = (bg_img->type == css::CSSValue::Type::URL) ? bg_img->string_value : bg_img->keyword;
            if (val.empty()) val = bg_img->string_value;

            // url() background image
            if (val.size() >= 4 && val.substr(0, 4) == "url(" && val.back() == ')') {
                std::string url = val.substr(4, val.size() - 5);
                // Strip quotes
                if (url.size() >= 2 && (url[0] == '"' || url[0] == '\'') && url.back() == url[0])
                    url = url.substr(1, url.size() - 2);
                if (!url.empty() && images_) {
                    auto it = images_->find(url);
                    if (it != images_->end() && it->second) {
                        auto *img = it->second.get();
                        f32 img_w = static_cast<f32>(img->width);
                        f32 img_h = static_cast<f32>(img->height);
                        if (img_w <= 0) img_w = bw;
                        if (img_h <= 0) img_h = bh;
                        ImageId id = reinterpret_cast<ImageId>(img);

                        // Check background-repeat
                        std::string repeat = "repeat";
                        auto *bg_rep = node->style().get("background-repeat");
                        if (bg_rep && bg_rep->type == css::CSSValue::Type::KEYWORD)
                            repeat = bg_rep->keyword;

                        // Check background-size
                        f32 draw_w = img_w;
                        f32 draw_h = img_h;
                        auto *bg_size = node->style().get("background-size");
                        if (bg_size && bg_size->type == css::CSSValue::Type::KEYWORD) {
                            if (bg_size->keyword == "cover") {
                                f32 scale = std::max(bw / img_w, bh / img_h);
                                draw_w = img_w * scale;
                                draw_h = img_h * scale;
                            } else if (bg_size->keyword == "contain") {
                                f32 scale = std::min(bw / img_w, bh / img_h);
                                draw_w = img_w * scale;
                                draw_h = img_h * scale;
                            }
                        }

                        // Check background-position
                        f32 pos_x = bx;
                        f32 pos_y = by;
                        auto *bg_pos = node->style().get("background-position");
                        if (bg_pos && bg_pos->type == css::CSSValue::Type::KEYWORD) {
                            if (bg_pos->keyword == "center" || bg_pos->keyword == "center center") {
                                pos_x = bx + (bw - draw_w) / 2.0f;
                                pos_y = by + (bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "top") {
                                pos_x = bx + (bw - draw_w) / 2.0f;
                                pos_y = by;
                            } else if (bg_pos->keyword == "bottom") {
                                pos_x = bx + (bw - draw_w) / 2.0f;
                                pos_y = by + bh - draw_h;
                            } else if (bg_pos->keyword == "left") {
                                pos_x = bx;
                                pos_y = by + (bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "right") {
                                pos_x = bx + bw - draw_w;
                                pos_y = by + (bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "top left" || bg_pos->keyword == "left top") {
                                pos_x = bx;
                                pos_y = by;
                            } else if (bg_pos->keyword == "top right" || bg_pos->keyword == "right top") {
                                pos_x = bx + bw - draw_w;
                                pos_y = by;
                            } else if (bg_pos->keyword == "bottom left" || bg_pos->keyword == "left bottom") {
                                pos_x = bx;
                                pos_y = by + bh - draw_h;
                            } else if (bg_pos->keyword == "bottom right" || bg_pos->keyword == "right bottom") {
                                pos_x = bx + bw - draw_w;
                                pos_y = by + bh - draw_h;
                            }
                        }

                        if (repeat == "no-repeat") {
                            list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                {pos_x, pos_y, draw_w, draw_h}, Color::WHITE, "", 0, id));
                        } else if (repeat == "repeat-x") {
                            for (f32 tx = pos_x; tx < bx + bw; tx += draw_w) {
                                list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                    {tx, pos_y, draw_w, draw_h}, Color::WHITE, "", 0, id));
                            }
                        } else if (repeat == "repeat-y") {
                            for (f32 ty = pos_y; ty < by + bh; ty += draw_h) {
                                list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                    {pos_x, ty, draw_w, draw_h}, Color::WHITE, "", 0, id));
                            }
                        } else {
                            // repeat: tile both directions
                            for (f32 tx = pos_x - std::fmod(pos_x - bx, draw_w); tx < bx + bw; tx += draw_w) {
                                for (f32 ty = pos_y - std::fmod(pos_y - by, draw_h); ty < by + bh; ty += draw_h) {
                                    list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                        {tx, ty, draw_w, draw_h}, Color::WHITE, "", 0, id));
                                }
                            }
                        }
                    }
                }
            }
            // Gradient strings from shorthand are handled here if needed — but direct gradient
            // declarations use GRADIENT type and are handled above
        }

        Color bg = resolve_color(node->style(), "background-color", Color::TRANSPARENT);
        if (bg.a == 0.0f)
            return;

        auto *br = node->style().get("border-top-left-radius");
        if (!br) br = node->style().get("border-radius");
        f32 radius = 0;
        if (br && br->type == css::CSSValue::Type::LENGTH) {
            radius = br->length.value;
        } else if (br && br->type == css::CSSValue::Type::STRING) {
            // Try parsing a single value from combined string
            char *end = nullptr;
            f32 num = std::strtof(br->string_value.c_str(), &end);
            if (end != br->string_value.c_str() && num > 0) {
                radius = num;
            }
        }

        if (radius > 0) {
            list.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT, {bx, by, bw, bh}, bg, "", 0, 0, {}, radius));
        } else {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, bh}, bg));
        }
    }

    void Painter::paint_shadow(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *bs = node->style().get("box-shadow");
        if (!bs || bs->type != css::CSSValue::Type::SHADOW_LIST || bs->shadows.empty())
            return;

        for (const auto &sh : bs->shadows) {
            f32 off_x = sh.offset_x;
            f32 off_y = sh.offset_y;
            f32 blur = sh.blur_radius;
            f32 spread = sh.spread_radius;
            Color shadow_color = css_to_render_color(sh.color);

            f32 bx = ox - node->padding.left + off_x - spread;
            f32 by = oy - node->padding.top + off_y - spread;
            f32 bw =
                node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right +
                2 * spread;
            f32 bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                     node->border.bottom + 2 * spread;

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

    void Painter::paint_text_shadow(DisplayList &list, css::LayoutNode *node, f32 ox, f32 oy) const {
        auto *ts = node->style().get("text-shadow");
        if (!ts || ts->type != css::CSSValue::Type::SHADOW_LIST || ts->shadows.empty())
            return;

        f32 font_size = resolve_font_size(node->style());

        for (const auto &sh : ts->shadows) {
            f32 off_x = sh.offset_x;
            f32 off_y = sh.offset_y;
            Color shadow_color = css_to_render_color(sh.color);

            // Paint shadow text at offset position
            if (!node->text_lines.empty()) {
                for (auto &li : node->text_lines) {
                    // Apply text-transform for shadow too
                    auto *tt = node->style().get("text-transform");
                    std::string text = li.text;
                    if (tt && tt->type == css::CSSValue::Type::KEYWORD) {
                        if (tt->keyword == "uppercase") {
                            for (char &c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        } else if (tt->keyword == "lowercase") {
                            for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        } else if (tt->keyword == "capitalize") {
                            bool new_word = true;
                            for (char &c : text) {
                                if (c == ' ' || c == '\t' || c == '\n') { new_word = true; continue; }
                                if (new_word) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); new_word = false; }
                            }
                        }
                    }
                    css::Rect line_rect = {ox + off_x, oy + li.y + off_y, node->content.width, font_size};
                    list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                                       line_rect,
                                       shadow_color,
                                       text,
                                       font_size));
                }
            } else {
                std::string text = node->text();
                auto *tt = node->style().get("text-transform");
                if (tt && tt->type == css::CSSValue::Type::KEYWORD) {
                    if (tt->keyword == "uppercase") {
                        for (char &c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    } else if (tt->keyword == "lowercase") {
                        for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    } else if (tt->keyword == "capitalize") {
                        bool new_word = true;
                        for (char &c : text) {
                            if (c == ' ' || c == '\t' || c == '\n') { new_word = true; continue; }
                            if (new_word) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); new_word = false; }
                        }
                    }
                }
                list.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                                   {ox + off_x, oy + off_y, node->content.width, node->content.height},
                                   shadow_color,
                                   text,
                                   font_size));
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
        // Paint text-shadow first (behind the text)
        paint_text_shadow(list, node, ox, oy);

        Color text_color = resolve_color(node->style(), "color", Color::BLACK);
        f32 font_size = resolve_font_size(node->style());

        // Descender pad from actual font metrics, fallback: 0.25 * font_size
        f32 descender_pad = std::ceil(font_size * 0.25f);
        if (text_renderer_) {
            auto fm = text_renderer_->get_font_metrics((u32)font_size);
            f32 metrics_pad = std::ceil(std::abs(fm.descender));
            if (metrics_pad >= 1)
                descender_pad = metrics_pad;
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
        Color dec_color = text_color;
        bool has_underline = false;
        bool has_overline = false;
        bool has_line_through = false;
        f32 dec_thickness = std::max(1.0f, font_size / 14.0f);

        // text-decoration-line takes priority
        auto *dec_line = node->style().get("text-decoration-line");
        if (dec_line && dec_line->type == css::CSSValue::Type::KEYWORD) {
            const std::string &dl = dec_line->keyword;
            if (dl.find("underline") != std::string::npos) has_underline = true;
            if (dl.find("overline") != std::string::npos) has_overline = true;
            if (dl.find("line-through") != std::string::npos) has_line_through = true;
            if (dl == "none") { has_underline = false; has_overline = false; has_line_through = false; }
        }
        // Fallback to text-decoration shorthand
        if (!has_underline && !has_overline && !has_line_through) {
            auto *dec_val = node->style().get("text-decoration");
            if (dec_val && dec_val->type == css::CSSValue::Type::KEYWORD) {
                const std::string &dk = dec_val->keyword;
                if (dk.find("underline") != std::string::npos) has_underline = true;
                if (dk.find("overline") != std::string::npos) has_overline = true;
                if (dk.find("line-through") != std::string::npos) has_line_through = true;
                if (dk == "none") { has_underline = false; has_overline = false; has_line_through = false; }
            }
        }
        // text-decoration-color
        auto *dec_color_val = node->style().get("text-decoration-color");
        if (dec_color_val) {
            Color c = resolve_color(node->style(), "text-decoration-color", text_color);
            if (c.r != 0 || c.g != 0 || c.b != 0 || c.a != 0) dec_color = c;
        }
        // text-decoration-thickness
        auto *dec_thick = node->style().get("text-decoration-thickness");
        if (dec_thick && dec_thick->type == css::CSSValue::Type::LENGTH) {
            dec_thickness = dec_thick->length.value;
        }

        auto paint_decorations = [&](f32 line_y, f32 line_w) {
            if (has_underline) {
                f32 y = line_y + font_size + 1.0f;
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {ox, y, line_w, dec_thickness}, dec_color));
            }
            if (has_overline) {
                f32 y = line_y - descender_pad * 0.3f;
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {ox, y, line_w, dec_thickness}, dec_color));
            }
            if (has_line_through) {
                f32 y = line_y + font_size * 0.4f;
                list.push(make_cmd(PaintCommand::Type::FILL_RECT, {ox, y, line_w, dec_thickness}, dec_color));
            }
        };

        bool any_deco = has_underline || has_overline || has_line_through;

        // Text transform (uppercase, lowercase, capitalize)
        auto apply_text_transform = [&](std::string s) -> std::string {
            auto *tt = node->style().get("text-transform");
            if (!tt || tt->type != css::CSSValue::Type::KEYWORD)
                return s;
            if (tt->keyword == "uppercase") {
                for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            } else if (tt->keyword == "lowercase") {
                for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else if (tt->keyword == "capitalize") {
                bool new_word = true;
                for (char &c : s) {
                    if (c == ' ' || c == '\t' || c == '\n') { new_word = true; continue; }
                    if (new_word) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); new_word = false; }
                }
            }
            return s;
        };

        if (!node->text_lines.empty()) {
            for (auto &li : node->text_lines) {
                std::string transformed = apply_text_transform(li.text);
                css::Rect line_rect = {ox, oy + li.y, node->content.width, font_size + descender_pad};
                auto tc = make_cmd(PaintCommand::Type::DRAW_TEXT,
                                   line_rect,
                                   text_color,
                                   transformed,
                                   font_size,
                                   0,
                                   {},
                                   0,
                                   {},
                                   1.0f,
                                   font_flags);
                list.push(tc);
                if (any_deco) {
                    paint_decorations(oy + li.y, node->content.width);
                }
            }
        } else {
            // No wrapping info — single line fallback
            std::string transformed = apply_text_transform(node->text());
            auto tc = make_cmd(PaintCommand::Type::DRAW_TEXT,
                               {ox, oy, node->content.width, node->content.height + descender_pad},
                               text_color,
                               transformed,
                               font_size,
                               0,
                               {},
                               0,
                               {},
                               1.0f,
                               font_flags);
            list.push(tc);
            if (any_deco) {
                paint_decorations(oy, node->content.width);
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
        f32 img_w = static_cast<f32>(img->width);
        f32 img_h = static_cast<f32>(img->height);
        if (img_w <= 0 || img_h <= 0)
            return;

        ImageId id = reinterpret_cast<ImageId>(img);
        f32 box_w = node->content.width > 0 ? node->content.width : img_w;
        f32 box_h = node->content.height > 0 ? node->content.height : img_h;

        // object-fit: fill (default), contain, cover, none, scale-down
        std::string ofit = "fill";
        auto *of = node->style().get("object-fit");
        if (of && of->type == css::CSSValue::Type::KEYWORD)
            ofit = of->keyword;

        f32 img_aspect = img_w / img_h;
        f32 box_aspect = box_w / box_h;
        f32 draw_w = box_w;
        f32 draw_h = box_h;
        f32 draw_x = ox;
        f32 draw_y = oy;
        bool needs_clip = false;

        if (ofit == "contain") {
            if (img_aspect > box_aspect) {
                draw_w = box_w;
                draw_h = box_w / img_aspect;
            } else {
                draw_h = box_h;
                draw_w = box_h * img_aspect;
            }
            draw_x = ox + (box_w - draw_w) / 2.0f;
            draw_y = oy + (box_h - draw_h) / 2.0f;
        } else if (ofit == "cover") {
            if (img_aspect > box_aspect) {
                draw_h = box_h;
                draw_w = box_h * img_aspect;
            } else {
                draw_w = box_w;
                draw_h = box_w / img_aspect;
            }
            draw_x = ox + (box_w - draw_w) / 2.0f;
            draw_y = oy + (box_h - draw_h) / 2.0f;
            needs_clip = true;
        } else if (ofit == "none") {
            draw_w = img_w;
            draw_h = img_h;
            draw_x = ox + (box_w - draw_w) / 2.0f;
            draw_y = oy + (box_h - draw_h) / 2.0f;
        } else if (ofit == "scale-down") {
            // Like none or contain, whichever gives smaller image
            if (img_w <= box_w && img_h <= box_h) {
                draw_w = img_w;
                draw_h = img_h;
                draw_x = ox + (box_w - draw_w) / 2.0f;
                draw_y = oy + (box_h - draw_h) / 2.0f;
            } else {
                if (img_aspect > box_aspect) {
                    draw_w = box_w;
                    draw_h = box_w / img_aspect;
                } else {
                    draw_h = box_h;
                    draw_w = box_h * img_aspect;
                }
                draw_x = ox + (box_w - draw_w) / 2.0f;
                draw_y = oy + (box_h - draw_h) / 2.0f;
            }
        }

        if (needs_clip) {
            list.push(make_cmd(PaintCommand::Type::PUSH_CLIP, {ox, oy, box_w, box_h}, Color::TRANSPARENT));
        }

        list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE, {draw_x, draw_y, draw_w, draw_h}, Color::WHITE, "", 0, id));

        if (needs_clip) {
            list.push(make_cmd(PaintCommand::Type::POP_CLIP, {}, Color::TRANSPARENT));
        }
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

        // Determine outline style
        std::string ostyle = "solid";
        if (os && os->type == css::CSSValue::Type::KEYWORD) ostyle = os->keyword;

        if (ostyle == "dotted") {
            // Draw dots along each edge
            f32 dot_size = outline_width;
            f32 gap = outline_width * 2.0f;
            f32 step = dot_size + gap;
            // Top edge
            for (f32 dx = bx; dx < bx + bw; dx += step) {
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {dx, by, std::min(dot_size, bx + bw - dx), dot_size}, outline_color));
            }
            // Bottom edge
            for (f32 dx = bx; dx < bx + bw; dx += step) {
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {dx, by + bh - dot_size, std::min(dot_size, bx + bw - dx), dot_size}, outline_color));
            }
            // Left edge
            for (f32 dy = by + step; dy < by + bh - step; dy += step) {
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {bx, dy, dot_size, std::min(dot_size, by + bh - dy)}, outline_color));
            }
            // Right edge
            for (f32 dy = by + step; dy < by + bh - step; dy += step) {
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {bx + bw - dot_size, dy, dot_size, std::min(dot_size, by + bh - dy)}, outline_color));
            }
        } else if (ostyle == "dashed") {
            // Draw dashes along each edge
            f32 dash_len = outline_width * 4.0f;
            f32 gap = outline_width * 2.0f;
            f32 step = dash_len + gap;
            // Top edge
            for (f32 dx = bx; dx < bx + bw; dx += step) {
                f32 len = std::min(dash_len, bx + bw - dx);
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {dx, by, len, outline_width}, outline_color));
            }
            // Bottom edge
            for (f32 dx = bx; dx < bx + bw; dx += step) {
                f32 len = std::min(dash_len, bx + bw - dx);
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {dx, by + bh - outline_width, len, outline_width}, outline_color));
            }
            // Left edge
            for (f32 dy = by + step; dy < by + bh - step; dy += step) {
                f32 len = std::min(dash_len, by + bh - dy);
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {bx, dy, outline_width, len}, outline_color));
            }
            // Right edge
            for (f32 dy = by + step; dy < by + bh - step; dy += step) {
                f32 len = std::min(dash_len, by + bh - dy);
                list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                    {bx + bw - outline_width, dy, outline_width, len}, outline_color));
            }
        } else if (ostyle == "double") {
            f32 db_order = outline_width / 3.0f;
            if (db_order < 1.0f) db_order = 1.0f;
            // Outer
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, db_order}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - db_order, by, db_order, bh}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - db_order, bw, db_order}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, db_order, bh}, outline_color));
            // Inner
            f32 inner_off = db_order * 2;
            list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                {bx + inner_off, by + inner_off, bw - 2 * inner_off, db_order}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                {bx + bw - db_order - inner_off, by + inner_off, db_order, bh - 2 * inner_off}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                {bx + inner_off, by + bh - db_order - inner_off, bw - 2 * inner_off, db_order}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT,
                {bx + inner_off, by + inner_off, db_order, bh - 2 * inner_off}, outline_color));
        } else if (ostyle == "groove" || ostyle == "ridge") {
            Color c1 = outline_color;
            Color c2 = {c1.r * 0.5f, c1.g * 0.5f, c1.b * 0.5f, c1.a};
            bool dark_first = (ostyle == "groove");
            Color outer_c = dark_first ? c2 : c1;
            Color inner_c = dark_first ? c1 : c2;
            f32 hw = outline_width / 2.0f;
            if (hw < 1.0f) hw = 1.0f;
            // Outer half
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, hw}, outer_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - hw, by, hw, bh}, outer_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - hw, bw, hw}, outer_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, hw, bh}, outer_c));
            // Inner half
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + hw, by + hw, bw - 2 * hw, hw}, inner_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - hw - hw, by + hw, hw, bh - 2 * hw}, inner_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + hw, by + bh - hw - hw, bw - 2 * hw, hw}, inner_c));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + hw, by + hw, hw, bh - 2 * hw}, inner_c));
        } else if (ostyle == "inset" || ostyle == "outset") {
            bool dark_inner = (ostyle == "inset");
            Color c_top, c_bottom, c_s1, c_s2;
            if (dark_inner) {
                c_top = {outline_color.r * 0.5f, outline_color.g * 0.5f, outline_color.b * 0.5f, outline_color.a};
                c_s1 = {outline_color.r * 0.7f, outline_color.g * 0.7f, outline_color.b * 0.7f, outline_color.a};
                c_s2 = {outline_color.r * 1.3f, outline_color.g * 1.3f, outline_color.b * 1.3f, outline_color.a};
                c_bottom = {std::min(outline_color.r * 1.6f, 1.0f), std::min(outline_color.g * 1.6f, 1.0f),
                            std::min(outline_color.b * 1.6f, 1.0f), outline_color.a};
            } else {
                c_top = {std::min(outline_color.r * 1.5f, 1.0f), std::min(outline_color.g * 1.5f, 1.0f),
                         std::min(outline_color.b * 1.5f, 1.0f), outline_color.a};
                c_s1 = {outline_color.r * 1.2f, outline_color.g * 1.2f, outline_color.b * 1.2f, outline_color.a};
                c_s2 = {outline_color.r * 0.7f, outline_color.g * 0.7f, outline_color.b * 0.7f, outline_color.a};
                c_bottom = {outline_color.r * 0.5f, outline_color.g * 0.5f, outline_color.b * 0.5f, outline_color.a};
            }
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, outline_width}, c_top));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, outline_width, bh}, c_s1));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - outline_width, by, outline_width, bh}, c_s2));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - outline_width, bw, outline_width}, c_bottom));
        } else {
            // solid (default)
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, bw, outline_width}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx + bw - outline_width, by, outline_width, bh}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by + bh - outline_width, bw, outline_width}, outline_color));
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {bx, by, outline_width, bh}, outline_color));
        }
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
