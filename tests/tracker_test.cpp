#include "test_framework.hpp"
#include "../net/tracker_blocker.hpp"

namespace browser::net {

TEST(tracker_default_list, {
    TrackerBlocker tb;
    tb.load_default_list();
    ASSERT(tb.should_block("https://www.doubleclick.net/ads?q=1"));
    ASSERT(tb.should_block("https://google-analytics.com/collect"));
    ASSERT(!tb.should_block("https://www.google.com/search"));
    ASSERT(!tb.should_block("https://example.com"));
    ASSERT(tb.blocked_count() > 0);
})

TEST(tracker_no_list, {
    TrackerBlocker tb;
    ASSERT(!tb.should_block("https://doubleclick.net/x"));
})

TEST(tracker_blocked_count, {
    TrackerBlocker tb;
    tb.load_default_list();
    tb.should_block("https://doubleclick.net/a");
    tb.should_block("https://google-analytics.com/b");
    tb.should_block("https://google.com/c");
    ASSERT(tb.blocked_count() == 2);
    tb.reset_count();
    ASSERT(tb.blocked_count() == 0);
})

TEST(tracker_subdomain, {
    TrackerBlocker tb;
    tb.load_default_list();
    ASSERT(tb.should_block("https://sub.doubleclick.net/x"));
    ASSERT(tb.should_block("https://a.b.c.doubleclick.net/x"));
})

TEST(tracker_no_false_positive, {
    TrackerBlocker tb;
    tb.load_default_list();
    ASSERT(!tb.should_block("https://notdoubleclick.net/x"));
    ASSERT(!tb.should_block("https://example-google-analytics.com/x"));
})

}
