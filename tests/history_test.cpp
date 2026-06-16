#include "test_framework.hpp"
#include "../browser/history.hpp"
#include <string>

namespace browser {

TEST(history_empty, {
    HistoryManager h;
    ASSERT(!h.can_go_back());
    ASSERT(!h.can_go_forward());
    ASSERT(h.current_url() == "");
})

TEST(history_push, {
    HistoryManager h;
    h.push("https://example.com", "Example");
    ASSERT(h.current_url() == "https://example.com");
    ASSERT(!h.can_go_back());
    ASSERT(!h.can_go_forward());
})

TEST(history_back_forward, {
    HistoryManager h;
    h.push("https://a.com", "A");
    h.push("https://b.com", "B");
    ASSERT(h.current_url() == "https://b.com");
    ASSERT(h.can_go_back());
    ASSERT(!h.can_go_forward());

    auto url = h.go_back();
    ASSERT(url.has_value());
    ASSERT(url.value() == "https://a.com");
    ASSERT(!h.can_go_back());
    ASSERT(h.can_go_forward());

    url = h.go_forward();
    ASSERT(url.has_value());
    ASSERT(url.value() == "https://b.com");
})

TEST(history_push_truncates_forward, {
    HistoryManager h;
    h.push("https://a.com", "A");
    h.push("https://b.com", "B");
    h.go_back();
    h.push("https://c.com", "C");  // should discard B
    ASSERT(!h.can_go_forward());
    ASSERT(h.current_url() == "https://c.com");
})

TEST(history_max_entries, {
    HistoryManager h(3);
    h.push("https://1.com", "1");
    h.push("https://2.com", "2");
    h.push("https://3.com", "3");
    h.push("https://4.com", "4");  // should evict 1
    ASSERT(h.can_go_back());      // 2 and 3 remain before 4
    auto url = h.go_back();
    ASSERT(url.has_value());
    ASSERT(url.value() == "https://3.com");
})

TEST(history_go_back_empty_returns_nullopt, {
    HistoryManager h;
    auto url = h.go_back();
    ASSERT(!url.has_value());
})

TEST(history_go_forward_empty_returns_nullopt, {
    HistoryManager h;
    h.push("https://a.com", "A");
    auto url = h.go_forward();
    ASSERT(!url.has_value());
})

} // namespace browser
