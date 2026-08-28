#pragma once
#include "../../core/utility.hpp"
#include <string>

namespace browser {

// Audit Wave 4: AddressBarEditor owning edit_buffer/cursor_pos/sel_start/all_selected/blink
// Extracted from BrowserWindow::Chrome edit state (event_handler.cpp:1282 + toolbar.cpp:57)
struct AddressBarEditor {
    std::string edit_buffer;
    u32 cursor_pos = 0;
    u32 sel_start = 0;
    bool all_selected = false;
    bool has_selection() const { return sel_start != cursor_pos; }
    void clear() { edit_buffer.clear(); cursor_pos = 0; sel_start = 0; all_selected = false; }
    void focus(const std::string& url) { edit_buffer = url; cursor_pos = static_cast<u32>(edit_buffer.size()); sel_start = 0; all_selected = false; }
    std::string selected_text() const {
        if (!has_selection()) return "";
        u32 a = std::min(sel_start, cursor_pos);
        u32 b = std::max(sel_start, cursor_pos);
        if (a > edit_buffer.size()) a = static_cast<u32>(edit_buffer.size());
        if (b > edit_buffer.size()) b = static_cast<u32>(edit_buffer.size());
        return edit_buffer.substr(a, b - a);
    }
    void insert_text(const std::string& t) {
        if (has_selection()) {
            u32 a = std::min(sel_start, cursor_pos);
            u32 b = std::max(sel_start, cursor_pos);
            edit_buffer.erase(a, b - a);
            cursor_pos = a;
            sel_start = a;
        }
        edit_buffer.insert(cursor_pos, t);
        cursor_pos += static_cast<u32>(t.size());
        sel_start = cursor_pos;
    }
    void backspace() {
        if (has_selection()) {
            u32 a = std::min(sel_start, cursor_pos);
            u32 b = std::max(sel_start, cursor_pos);
            edit_buffer.erase(a, b - a);
            cursor_pos = a; sel_start = a;
            return;
        }
        if (cursor_pos > 0 && !edit_buffer.empty()) {
            // Byte-indexed for now (BR-C9: should be code-point)
            edit_buffer.erase(cursor_pos - 1, 1);
            cursor_pos--;
            sel_start = cursor_pos;
        }
    }
};

} // namespace browser
