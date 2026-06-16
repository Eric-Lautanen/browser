#include "history.hpp"

namespace browser {

HistoryManager::HistoryManager(u32 max)
    : max_(max) {}

void HistoryManager::push(const std::string& url, const std::string& title) {
    if (!entries_.empty() && index_ < entries_.size() - 1) {
        entries_.resize(index_ + 1);
    }
    if (entries_.size() >= max_) {
        entries_.erase(entries_.begin());
    }
    entries_.push_back({url, title});
    index_ = static_cast<u32>(entries_.size()) - 1;
}

bool HistoryManager::can_go_back() const {
    return !entries_.empty() && index_ > 0;
}

bool HistoryManager::can_go_forward() const {
    if (entries_.empty()) return false;
    return index_ < entries_.size() - 1;
}

std::optional<std::string> HistoryManager::go_back() {
    if (entries_.empty() || index_ == 0) return std::nullopt;
    index_--;
    return entries_[index_].url;
}

std::optional<std::string> HistoryManager::go_forward() {
    if (entries_.empty() || index_ >= entries_.size() - 1) return std::nullopt;
    index_++;
    return entries_[index_].url;
}

std::string HistoryManager::current_url() const {
    if (entries_.empty()) return "";
    return entries_[index_].url;
}

}
