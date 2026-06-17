#pragma once
#include "../tests/utility.hpp"

#include <string>
#include <vector>

namespace browser {

struct SessionEntry {
    std::string url;
    std::string title;
    i32 scroll_y = 0;
};

class SessionManager {
public:
    SessionManager();

    void save(const std::vector<SessionEntry>& tabs);
    std::vector<SessionEntry> load();
    bool previous_session_crashed() const;
    void clear();

private:
    std::string path_ = "./session.dat";
    std::string lock_path_ = "./session.lock";
};

}  // namespace browser
