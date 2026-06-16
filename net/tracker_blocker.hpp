#pragma once
#include <string>
#include <vector>
#include "../tests/utility.hpp"

namespace browser::net {

class TrackerBlocker {
public:
    TrackerBlocker();
    void load_default_list();
    bool should_block(const std::string& url) const;
    u32 blocked_count() const;
    void reset_count();
private:
    struct Rule { std::string domain; };
    std::vector<Rule> rules_;
    mutable u32 blocked_count_ = 0;
};

}

