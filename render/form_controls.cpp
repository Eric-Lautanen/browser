#include "form_controls.hpp"

#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace browser::render::form_controls {

    namespace {

        PaintCommand make_cmd(PaintCommand::Type type,
                              css::Rect rect,
                              const Color &color,
                              const std::string &text = "",
                              f32 font_size = 16,
                              ImageId image_id = 0,
                              const css::CSSGradient &gradient = {},
                              f32 radius = 0) {
            PaintCommand cmd;
            cmd.type = type;
            cmd.rect = rect;
            cmd.color = color;
            cmd.text = text;
            cmd.font_size = font_size;
            cmd.image_id = image_id;
            cmd.gradient = gradient;
            cmd.radius = radius;
            return cmd;
        }

    }  // namespace

    void paint_text_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, u32 caret_pos, bool focused) {
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, {1, 1, 1, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));

        f32 text_x = x + 3;
        f32 text_y = y + (h - 14) / 2.0f;
        commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {text_x, text_y, w - 6, h}, {0, 0, 0, 1}, value, 14));

        if (focused) {
            f32 cx = text_x + static_cast<f32>(caret_pos) * 7.0f;
            if (cx < x + w - 2)
                commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {cx, y + 2, 1, h - 4}, {0, 0, 0, 1}));
        }
    }

    void paint_button(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &label, bool hovered, bool active) {
        Color bg = {0.94f, 0.94f, 0.94f, 1.0f};
        if (active) {
            bg = {0.85f, 0.85f, 0.85f, 1.0f};
        } else if (hovered) {
            bg = {0.90f, 0.90f, 0.90f, 1.0f};
        }

        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, bg));

        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.8f, 0.8f, 0.8f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.8f, 0.8f, 0.8f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));

        if (active) {
            commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
            commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));
        }

        f32 tw = static_cast<f32>(label.size()) * 7.0f;
        f32 tx = x + (w - tw) / 2.0f;
        f32 ty = y + (h - 14) / 2.0f;
        if (active) {
            tx += 1;
            ty += 1;
        }
        commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {tx, ty, w, h}, {0, 0, 0, 1}, label, 14));
    }

    void paint_checkbox(CommandList &commands, f32 x, f32 y, f32 size, bool checked) {
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, size, size}, {1, 1, 1, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, size, 1}, {0.4f, 0.4f, 0.4f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + size - 1, size, 1}, {0.4f, 0.4f, 0.4f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, size}, {0.4f, 0.4f, 0.4f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + size - 1, y, 1, size}, {0.4f, 0.4f, 0.4f, 1}));

        if (checked) {
            commands.push(
                make_cmd(PaintCommand::Type::DRAW_TEXT, {x + 2, y, size, size}, {0, 0, 0, 1}, "\xe2\x9c\x93", 12));
        }
    }

    void paint_radio(CommandList &commands, f32 x, f32 y, f32 size, bool checked) {
        commands.push(make_cmd(
            PaintCommand::Type::DRAW_ROUNDED_RECT, {x, y, size, size}, {1, 1, 1, 1}, "", 0, 0, {}, size / 2.0f));
        commands.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT,
                               {x, y, size, size},
                               {0.4f, 0.4f, 0.4f, 1},
                               "",
                               0,
                               0,
                               {},
                               size / 2.0f));

        if (checked) {
            f32 inner = size * 0.4f;
            f32 ix = x + (size - inner) / 2.0f;
            f32 iy = y + (size - inner) / 2.0f;
            commands.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT,
                                   {ix, iy, inner, inner},
                                   {0.3f, 0.3f, 0.3f, 1},
                                   "",
                                   0,
                                   0,
                                   {},
                                   inner / 2.0f));
        }
    }

    void paint_select(CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, bool open) {
        Color bg = open ? Color{0.85f, 0.85f, 0.85f, 1.0f} : Color{0.95f, 0.95f, 0.95f, 1.0f};
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, bg));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));

        commands.push(
            make_cmd(PaintCommand::Type::DRAW_TEXT, {x + 3, y + (h - 14) / 2.0f, w - 20, h}, {0, 0, 0, 1}, value, 14));

        // Down arrow
        f32 ax = x + w - 14;
        f32 ay = y + (h - 4) / 2.0f;
        commands.push(
            make_cmd(PaintCommand::Type::DRAW_TEXT, {ax, ay, 10, h}, {0.3f, 0.3f, 0.3f, 1}, "\xe2\x96\xbc", 10));
    }

    void paint_textarea(CommandList &commands,
                        f32 x,
                        f32 y,
                        f32 w,
                        f32 h,
                        const std::string &value,
                        u32 cursor_line,
                        u32 cursor_col,
                        bool focused) {
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, {1, 1, 1, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));

        f32 text_x = x + 3;
        f32 text_y = y + 3;
        f32 line_h = 16.0f;

        // Simple word wrap display: split by newlines
        size_t start = 0;
        u32 line = 0;
        f32 ly = text_y;
        f32 chars_per_line = std::max(1.0f, (w - 6) / 7.0f);

        for (size_t i = 0; i <= value.size(); i++) {
            if (i == value.size() || value[i] == '\n') {
                std::string line_text = value.substr(start, i - start);
                if (line_text.size() > chars_per_line)
                    line_text = line_text.substr(0, static_cast<size_t>(chars_per_line));
                commands.push(
                    make_cmd(PaintCommand::Type::DRAW_TEXT, {text_x, ly, w - 6, line_h}, {0, 0, 0, 1}, line_text, 14));

                if (focused && line == cursor_line) {
                    f32 cx = text_x + static_cast<f32>(cursor_col) * 7.0f;
                    if (cx < x + w - 2)
                        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {cx, ly, 1, line_h}, {0, 0, 0, 1}));
                }

                start = i + 1;
                line++;
                ly += line_h;
                if (ly > y + h - 3)
                    break;
            }
        }
    }

}  // namespace browser::render::form_controls
