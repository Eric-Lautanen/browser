#pragma once
#include "paint/commands.hpp"

#include <string>

namespace browser::render::form_controls {

    using CommandList = DisplayList;

    void paint_text_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, u32 caret_pos, bool focused);

    void paint_number_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, u32 caret_pos, bool focused,
        f32 spin_active);  // spin_active: -1=down, 0=none, 1=up

    void paint_button(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &label, bool hovered, bool active);

    void paint_checkbox(CommandList &commands, f32 x, f32 y, f32 size, bool checked);

    void paint_radio(CommandList &commands, f32 x, f32 y, f32 size, bool checked);

    void paint_select(CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, bool open);

    void paint_textarea(CommandList &commands,
                        f32 x,
                        f32 y,
                        f32 w,
                        f32 h,
                        const std::string &value,
                        u32 cursor_line,
                        u32 cursor_col,
                        bool focused);

    void paint_range(CommandList &commands,
                     f32 x, f32 y, f32 w, f32 h,
                     f32 value, f32 min_val, f32 max_val, bool focused);

    void paint_file_input(CommandList &commands,
                          f32 x, f32 y, f32 w, f32 h,
                          const std::string &filename, bool focused);

    void paint_progress(CommandList &commands,
                        f32 x, f32 y, f32 w, f32 h, f32 value, f32 max_val);

    void paint_color_input(CommandList &commands,
                           f32 x, f32 y, f32 w, f32 h, const std::string &value, bool focused);

}  // namespace browser::render::form_controls
