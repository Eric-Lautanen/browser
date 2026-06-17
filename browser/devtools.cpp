#include "devtools.hpp"

namespace browser {

void DevToolsState::add_console_entry(ConsoleEntry::Level level, const std::string& text) {
    ConsoleEntry entry;
    entry.level = level;
    entry.text = text;
    console_entries.push_back(entry);
    if (console_entries.size() > 1000) {
        console_entries.erase(console_entries.begin());
    }
}

void DevToolsState::clear_console() {
    console_entries.clear();
}

}  // namespace browser
