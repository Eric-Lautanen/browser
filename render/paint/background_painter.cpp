#include "background_painter.hpp"
#include "../../css/layout.hpp"
#include "../../image/decoder.hpp"
#include "paint_helpers.hpp"

namespace browser::render {

void paint_background_commands(DisplayList& list,
                               const css::LayoutNode* node,
                               float ox, float oy,
                               const std::unordered_map<std::string, std::shared_ptr<image::Image>>* images) {
        auto *bg_img = node->style().get("background-image");
        // Clip box (default border-box)
        f32 clip_bx = ox - node->padding.left;
        f32 clip_by = oy - node->padding.top;
        f32 clip_bw =
            node->content.width + node->padding.left + node->padding.right + node->border.left + node->border.right;
        f32 clip_bh =
            node->content.height + node->padding.top + node->padding.bottom + node->border.top + node->border.bottom;

        // Origin box for positioning (default padding-box)
        f32 origin_bx = ox - node->padding.left;
        f32 origin_by = oy - node->padding.top;
        f32 origin_bw = node->content.width + node->padding.left + node->padding.right;
        f32 origin_bh = node->content.height + node->padding.top + node->padding.bottom;

        // Apply background-clip
        auto *bg_clip = node->style().get("background-clip");
        if (bg_clip && bg_clip->type == css::CSSValue::Type::KEYWORD) {
            if (bg_clip->keyword == "padding-box") {
                clip_bx = ox - node->padding.left;
                clip_by = oy - node->padding.top;
                clip_bw = node->content.width + node->padding.left + node->padding.right;
                clip_bh = node->content.height + node->padding.top + node->padding.bottom;
            } else if (bg_clip->keyword == "content-box") {
                clip_bx = ox;
                clip_by = oy;
                clip_bw = node->content.width;
                clip_bh = node->content.height;
            }
            // border-box is the default
        }

        // Apply background-origin
        auto *bg_origin = node->style().get("background-origin");
        if (bg_origin && bg_origin->type == css::CSSValue::Type::KEYWORD) {
            if (bg_origin->keyword == "border-box") {
                origin_bx = ox - node->padding.left - node->border.left;
                origin_by = oy - node->padding.top - node->border.top;
                origin_bw = node->content.width + node->padding.left + node->padding.right + node->border.left +
                            node->border.right;
                origin_bh = node->content.height + node->padding.top + node->padding.bottom + node->border.top +
                            node->border.bottom;
            } else if (bg_origin->keyword == "content-box") {
                origin_bx = ox;
                origin_by = oy;
                origin_bw = node->content.width;
                origin_bh = node->content.height;
            }
            // padding-box is the default
        }

        if (bg_img && bg_img->type == css::CSSValue::Type::GRADIENT) {
            list.push(make_cmd(PaintCommand::Type::DRAW_GRADIENT,
                               {clip_bx, clip_by, clip_bw, clip_bh},
                               Color::TRANSPARENT,
                               "",
                               0,
                               0,
                               bg_img->gradient));
            return;
        }

        // Handle background-image from shorthand (KEYWORD with url() or gradient string)
        if (bg_img && (bg_img->type == css::CSSValue::Type::KEYWORD || bg_img->type == css::CSSValue::Type::STRING ||
                       bg_img->type == css::CSSValue::Type::URL)) {
            std::string val = (bg_img->type == css::CSSValue::Type::URL) ? bg_img->string_value : bg_img->keyword;
            if (val.empty())
                val = bg_img->string_value;

            // url() background image
            if (val.size() >= 4 && val.substr(0, 4) == "url(" && val.back() == ')') {
                std::string url = val.substr(4, val.size() - 5);
                // Strip quotes
                if (url.size() >= 2 && (url[0] == '"' || url[0] == '\'') && url.back() == url[0])
                    url = url.substr(1, url.size() - 2);
                if (!url.empty() && images) {
                    auto it = images->find(url);
                    if (it != images->end() && it->second) {
                        auto *img = it->second.get();
                        f32 img_w = static_cast<f32>(img->width);
                        f32 img_h = static_cast<f32>(img->height);
                        if (img_w <= 0)
                            img_w = clip_bw;
                        if (img_h <= 0)
                            img_h = clip_bh;
                        ImageId id = reinterpret_cast<ImageId>(img);

                        // Check background-repeat
                        std::string repeat = "repeat";
                        auto *bg_rep = node->style().get("background-repeat");
                        if (bg_rep && bg_rep->type == css::CSSValue::Type::KEYWORD)
                            repeat = bg_rep->keyword;

                        // Check background-size (uses clip box for sizing reference)
                        f32 draw_w = img_w;
                        f32 draw_h = img_h;
                        auto *bg_size = node->style().get("background-size");
                        if (bg_size && bg_size->type == css::CSSValue::Type::KEYWORD) {
                            if (bg_size->keyword == "cover") {
                                f32 scale = std::max(clip_bw / img_w, clip_bh / img_h);
                                draw_w = img_w * scale;
                                draw_h = img_h * scale;
                            } else if (bg_size->keyword == "contain") {
                                f32 scale = std::min(clip_bw / img_w, clip_bh / img_h);
                                draw_w = img_w * scale;
                                draw_h = img_h * scale;
                            }
                        }

                        // Check background-position (uses origin box)
                        f32 pos_x = origin_bx;
                        f32 pos_y = origin_by;
                        auto *bg_pos = node->style().get("background-position");
                        if (bg_pos && bg_pos->type == css::CSSValue::Type::KEYWORD) {
                            if (bg_pos->keyword == "center" || bg_pos->keyword == "center center") {
                                pos_x = origin_bx + (origin_bw - draw_w) / 2.0f;
                                pos_y = origin_by + (origin_bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "top") {
                                pos_x = origin_bx + (origin_bw - draw_w) / 2.0f;
                                pos_y = origin_by;
                            } else if (bg_pos->keyword == "bottom") {
                                pos_x = origin_bx + (origin_bw - draw_w) / 2.0f;
                                pos_y = origin_by + origin_bh - draw_h;
                            } else if (bg_pos->keyword == "left") {
                                pos_x = origin_bx;
                                pos_y = origin_by + (origin_bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "right") {
                                pos_x = origin_bx + origin_bw - draw_w;
                                pos_y = origin_by + (origin_bh - draw_h) / 2.0f;
                            } else if (bg_pos->keyword == "top left" || bg_pos->keyword == "left top") {
                                pos_x = origin_bx;
                                pos_y = origin_by;
                            } else if (bg_pos->keyword == "top right" || bg_pos->keyword == "right top") {
                                pos_x = origin_bx + origin_bw - draw_w;
                                pos_y = origin_by;
                            } else if (bg_pos->keyword == "bottom left" || bg_pos->keyword == "left bottom") {
                                pos_x = origin_bx;
                                pos_y = origin_by + origin_bh - draw_h;
                            } else if (bg_pos->keyword == "bottom right" || bg_pos->keyword == "right bottom") {
                                pos_x = origin_bx + origin_bw - draw_w;
                                pos_y = origin_by + origin_bh - draw_h;
                            }
                        }

                        if (repeat == "no-repeat") {
                            list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                               {pos_x, pos_y, draw_w, draw_h},
                                               Color::WHITE,
                                               "",
                                               0,
                                               id));
                        } else if (repeat == "repeat-x") {
                            for (f32 tx = pos_x; tx < origin_bx + origin_bw; tx += draw_w) {
                                list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                                   {tx, pos_y, draw_w, draw_h},
                                                   Color::WHITE,
                                                   "",
                                                   0,
                                                   id));
                            }
                        } else if (repeat == "repeat-y") {
                            for (f32 ty = pos_y; ty < origin_by + origin_bh; ty += draw_h) {
                                list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                                   {pos_x, ty, draw_w, draw_h},
                                                   Color::WHITE,
                                                   "",
                                                   0,
                                                   id));
                            }
                        } else {
                            // repeat: tile both directions (clip to clip box)
                            for (f32 tx = pos_x - std::fmod(pos_x - clip_bx, draw_w); tx < clip_bx + clip_bw;
                                 tx += draw_w) {
                                for (f32 ty = pos_y - std::fmod(pos_y - clip_by, draw_h); ty < clip_by + clip_bh;
                                     ty += draw_h) {
                                    list.push(make_cmd(PaintCommand::Type::DRAW_IMAGE,
                                                       {tx, ty, draw_w, draw_h},
                                                       Color::WHITE,
                                                       "",
                                                       0,
                                                       id));
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
        if (!br)
            br = node->style().get("border-radius");
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
            list.push(make_cmd(
                PaintCommand::Type::DRAW_ROUNDED_RECT, {clip_bx, clip_by, clip_bw, clip_bh}, bg, "", 0, 0, {}, radius));
        } else {
            list.push(make_cmd(PaintCommand::Type::FILL_RECT, {clip_bx, clip_by, clip_bw, clip_bh}, bg));
        }
}

} // namespace browser::render
