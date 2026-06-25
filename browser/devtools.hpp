#pragma once
#include "../html/dom.hpp"
#include "types.hpp"

#include <string>
#include <vector>

namespace browser {

struct DevToolsState {
    bool visible = false;
    enum Tab { CONSOLE, ELEMENTS, NETWORK };
    Tab active_tab = CONSOLE;

    struct ConsoleEntry {
        enum Level { LOG, WARN, LEVEL_ERROR, INFO, LEVEL_DEBUG };
        Level level;
        std::string text;
    };
    std::vector<ConsoleEntry> console_entries;
    std::string console_input;

    const html::Element* selected_element = nullptr;

    struct NetworkEntry {
        std::string url;
        std::string method;
        u16 status = 0;
        u64 start_ms = 0;
        u64 duration_ms = 0;
        u64 size = 0;
    };
    std::vector<NetworkEntry> network_entries;

    void add_console_entry(ConsoleEntry::Level level, const std::string& text);
    void clear_console();
};

}  // namespace browser
