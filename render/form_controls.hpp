#pragma once
#include "paint/commands.hpp"

#include <string>

namespace browser::render::form_controls {

    using CommandList = DisplayList;

    void paint_text_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, const std::string &placeholder,
        u32 caret_pos, bool focused,
        bool disabled = false, const Color &caret_color = {0, 0, 0, 1});

    void paint_number_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, const std::string &placeholder,
        u32 caret_pos, bool focused,
        f32 spin_active, bool disabled = false, const Color &caret_color = {0, 0, 0, 1});

    void paint_button(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &label, bool hovered, bool active);

    void paint_checkbox(CommandList &commands, f32 x, f32 y, f32 size, bool checked,
                        const Color &accent = {0.2f, 0.4f, 0.9f, 1});

    void paint_radio(CommandList &commands, f32 x, f32 y, f32 size, bool checked,
                     const Color &accent = {0.2f, 0.4f, 0.9f, 1});

    void paint_select(CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, bool open);

    void paint_textarea(CommandList &commands,
                        f32 x,
                        f32 y,
                        f32 w,
                        f32 h,
                        const std::string &value,
                        u32 cursor_line,
                        u32 cursor_col,
                        bool focused,
                        const std::string &resize = "both");

    void paint_range(CommandList &commands,
                     f32 x, f32 y, f32 w, f32 h,
                     f32 value, f32 min_val, f32 max_val, bool focused,
                     const Color &accent = {0.3f, 0.5f, 0.9f, 1});

    void paint_file_input(CommandList &commands,
                          f32 x, f32 y, f32 w, f32 h,
                          const std::string &filename, bool focused);

    void paint_progress(CommandList &commands,
                        f32 x, f32 y, f32 w, f32 h, f32 value, f32 max_val,
                        const Color &accent = {0.2f, 0.5f, 0.9f, 1});

    void paint_color_input(CommandList &commands,
                           f32 x, f32 y, f32 w, f32 h, const std::string &value, bool focused);

}  // namespace browser::render::form_controls
