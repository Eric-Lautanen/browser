#pragma once
#include "types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace browser {

class HistoryManager {
public:
    HistoryManager(u32 max = 100);
    void push(const std::string& url, const std::string& title);
    bool can_go_back() const;
    bool can_go_forward() const;
    std::optional<std::string> go_back();
    std::optional<std::string> go_forward();
    std::string current_url() const;
private:
    struct Entry { std::string url, title; };
    std::vector<Entry> entries_;
    u32 index_ = 0, max_;
};

}
