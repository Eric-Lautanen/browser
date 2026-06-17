#pragma once
#include "paint/commands.hpp"

#include <string>

namespace browser::render::form_controls {

    using CommandList = DisplayList;

    void paint_text_input(
        CommandList &commands, f32 x, f32 y, f32 w, f32 h, const std::string &value, u32 caret_pos, bool focused);

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

}  // namespace browser::render::form_controls
