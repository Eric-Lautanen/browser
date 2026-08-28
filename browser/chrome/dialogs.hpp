#pragma once
#include <string>
#include <optional>
#include <windows.h>

namespace browser {

// BR-C8: Per-instance dialog contexts via GWLP_USERDATA, not static globals
std::optional<std::string> show_date_picker(HWND parent, const std::string& cur_val);
std::optional<std::string> show_time_picker(HWND parent, const std::string& cur_val);
bool show_file_picker(HWND parent, std::string& out_path);
bool show_color_picker(HWND parent, std::string cur_val, std::string& out_color);

} // namespace browser
