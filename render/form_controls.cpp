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
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, const std::string &placeholder,
        u32 caret_pos, bool focused,
        bool disabled) {
        Color bg = disabled ? Color{0.95f, 0.95f, 0.95f, 1} : Color{1, 1, 1, 1};
        Color border_c = disabled ? Color{0.75f, 0.75f, 0.75f, 1} : Color{0.6f, 0.6f, 0.6f, 1};
        Color text_c = disabled ? Color{0.55f, 0.55f, 0.55f, 1} : Color{0, 0, 0, 1};
        Color placeholder_c = Color{0.55f, 0.55f, 0.55f, 1};

        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, bg));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, border_c));

        f32 text_x = x + 3;
        f32 text_y = y + (h - 14) / 2.0f;
        std::string display = value;
        Color display_color = text_c;
        if (display.empty() && !placeholder.empty() && !focused) {
            display = placeholder;
            display_color = placeholder_c;
        }
        commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {text_x, text_y, w - 6, h}, display_color, display, 14));

        if (focused && !disabled) {
            f32 cx = text_x + static_cast<f32>(caret_pos) * 7.0f;
            if (cx < x + w - 2)
                commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {cx, y + 2, 1, h - 4}, {0, 0, 0, 1}));
        }
    }

    void paint_number_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, const std::string &placeholder,
        u32 caret_pos, bool focused,
        f32 spin_active, bool disabled) {
        f32 spin_w = 18.0f;
        f32 text_w = w - spin_w;
        if (text_w < 10.0f) text_w = 10.0f;

        Color bg = disabled ? Color{0.95f, 0.95f, 0.95f, 1} : Color{1, 1, 1, 1};
        Color border_c = disabled ? Color{0.75f, 0.75f, 0.75f, 1} : Color{0.6f, 0.6f, 0.6f, 1};
        Color text_c = disabled ? Color{0.55f, 0.55f, 0.55f, 1} : Color{0, 0, 0, 1};

        // Input background and border
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, bg));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, border_c));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, border_c));

        // Spin button divider
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + text_w, y, 1, h}, border_c));

        // Spin up button
        f32 half_h = h / 2.0f;
        Color spin_arrow = disabled ? Color{0.65f, 0.65f, 0.65f, 1} : Color{0.2f, 0.2f, 0.2f, 1};
        Color up_bg = disabled ? Color{0.95f, 0.95f, 0.95f, 1} :
                     (spin_active == 1 ? Color{0.75f, 0.75f, 0.75f, 1} : Color{0.92f, 0.92f, 0.92f, 1});
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + text_w + 1, y, spin_w - 1, half_h}, up_bg));
        {
            f32 cx = x + text_w + spin_w / 2.0f;
            f32 cy = y + half_h / 2.0f;
            commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                {cx - 3, cy - 4, 8, 8}, spin_arrow, "\xe2\x96\xb2", 8));
        }

        // Spin down button
        Color dn_bg = disabled ? Color{0.95f, 0.95f, 0.95f, 1} :
                      (spin_active == -1 ? Color{0.75f, 0.75f, 0.75f, 1} : Color{0.92f, 0.92f, 0.92f, 1});
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + text_w + 1, y + half_h, spin_w - 1, half_h}, dn_bg));
        {
            f32 cx = x + text_w + spin_w / 2.0f;
            f32 cy = y + half_h + half_h / 2.0f;
            commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                {cx - 3, cy - 4, 8, 8}, spin_arrow, "\xe2\x96\xbc", 8));
        }

        // Separator between spin buttons
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + text_w + 1, y + half_h, spin_w - 1, 1}, border_c));

        // Text value
        f32 text_x = x + 3;
        f32 text_y = y + (h - 14) / 2.0f;
        std::string display = value;
        Color display_color = text_c;
        if (display.empty() && !placeholder.empty() && !focused) {
            display = placeholder;
            display_color = Color{0.55f, 0.55f, 0.55f, 1};
        }
        commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {text_x, text_y, text_w - 6, h}, display_color, display, 14));

        // Caret
        if (focused && !disabled) {
            f32 cx = text_x + static_cast<f32>(caret_pos) * 7.0f;
            if (cx < x + text_w - 2)
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

    void paint_range(CommandList &commands,
                     f32 x, f32 y, f32 w, f32 h,
                     f32 value, f32 min_val, f32 max_val, bool focused) {
        f32 track_h = 4.0f;
        f32 track_y = y + (h - track_h) / 2.0f;
        f32 thumb_r = 7.0f;

        // Track background
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, track_y, w, track_h}, {0.7f, 0.7f, 0.7f, 1}));

        // Filled track
        f32 range = max_val - min_val;
        f32 frac = (range > 0) ? (value - min_val) / range : 0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        f32 fill_w = w * frac;
        if (fill_w > 0) {
            commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, track_y, fill_w, track_h}, {0.3f, 0.5f, 0.9f, 1}));
        }

        // Track border
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, track_y, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, track_y + track_h - 1, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, track_y, 1, track_h}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, track_y, 1, track_h}, {0.5f, 0.5f, 0.5f, 1}));

        // Thumb
        f32 thumb_x = x + fill_w - thumb_r;
        if (thumb_x < x) thumb_x = x;
        if (thumb_x > x + w - thumb_r * 2) thumb_x = x + w - thumb_r * 2;
        f32 thumb_y = y + (h - thumb_r * 2) / 2.0f;
        Color thumb_color = focused ? Color{0.2f, 0.4f, 0.9f, 1} : Color{0.5f, 0.5f, 0.5f, 1};
        commands.push(make_cmd(PaintCommand::Type::DRAW_ROUNDED_RECT,
            {thumb_x, thumb_y, thumb_r * 2, thumb_r * 2}, thumb_color, "", 0, 0, {}, thumb_r));
    }

    void paint_file_input(CommandList &commands,
                          f32 x, f32 y, f32 w, f32 h,
                          const std::string &filename, bool focused) {
        f32 btn_w = 80.0f;
        // Button
        Color btn_bg = focused ? Color{0.85f, 0.85f, 0.85f, 1} : Color{0.94f, 0.94f, 0.94f, 1};
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, btn_w, h}, btn_bg));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, btn_w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, btn_w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + btn_w - 1, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));

        std::string label = filename.empty() ? "Choose File" : filename;
        f32 max_label_w = btn_w - 8;
        f32 approx_w = std::min(static_cast<f32>(label.size()) * 7.0f, max_label_w);
        f32 tx = x + (btn_w - approx_w) / 2.0f;
        commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT, {tx, y + (h - 14) / 2.0f, btn_w - 2, h}, {0, 0, 0, 1},
            label.size() > 12 ? label.substr(0, 10) + "..." : label, 12));

        // Filename display area
        f32 fn_x = x + btn_w + 4;
        f32 fn_w = w - btn_w - 4;
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {fn_x, y, fn_w, h}, {1, 1, 1, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {fn_x, y, fn_w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {fn_x, y + h - 1, fn_w, 1}, {0.6f, 0.6f, 0.6f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {fn_x + fn_w - 1, y, 1, h}, {0.6f, 0.6f, 0.6f, 1}));

        if (!filename.empty()) {
            commands.push(make_cmd(PaintCommand::Type::DRAW_TEXT,
                {fn_x + 3, y + (h - 14) / 2.0f, fn_w - 6, h}, {0.4f, 0.4f, 0.4f, 1}, filename, 13));
        }
    }

    void paint_progress(CommandList &commands,
                        f32 x, f32 y, f32 w, f32 h, f32 value, f32 max_val) {
        f32 frac = (max_val > 0) ? value / max_val : 0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;

        // Background track
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, {0.88f, 0.88f, 0.88f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));

        // Filled bar
        f32 fill_w = w * frac;
        if (fill_w > 0) {
            commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + 1, y + 1, fill_w - 2, h - 2}, {0.2f, 0.5f, 0.9f, 1}));
        }
    }

    void paint_color_input(CommandList &commands,
                           f32 x, f32 y, f32 w, f32 h, const std::string &value, bool focused) {
        // Parse color value (#RRGGBB or named)
        Color swatch = {0, 0, 0, 1};
        if (!value.empty() && value[0] == '#' && value.size() >= 7) {
            auto c = css::Color::from_hex(value);
            swatch = {static_cast<f32>(c.r) / 255.0f, static_cast<f32>(c.g) / 255.0f,
                      static_cast<f32>(c.b) / 255.0f, 1.0f};
        } else if (!value.empty()) {
            auto named = css::Color::from_name(value);
            if (named.a != 0 || value == "transparent")
                swatch = {static_cast<f32>(named.r) / 255.0f, static_cast<f32>(named.g) / 255.0f,
                          static_cast<f32>(named.b) / 255.0f, static_cast<f32>(named.a) / 255.0f};
        }

        f32 pad = 2.0f;
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, h}, {1, 1, 1, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y + h - 1, w, 1}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + w - 1, y, 1, h}, {0.5f, 0.5f, 0.5f, 1}));

        // Color swatch
        commands.push(make_cmd(PaintCommand::Type::FILL_RECT, {x + pad, y + pad, w - pad * 2, h - pad * 2}, swatch));

        // Focus indicator
        if (focused) {
            commands.push(make_cmd(PaintCommand::Type::FILL_RECT,
                {x + 1, y + 1, 2, 2}, {0.3f, 0.5f, 0.9f, 1}));
        }
    }

}  // namespace browser::render::form_controls
