#include "test_framework.hpp"
#include "../browser/bookmarks.hpp"
#include <cstdio>

namespace browser {

TEST(bookmark_add, {
    BookmarkManager bm;
    auto r = bm.add("https://example.com", "Example");
    ASSERT(r.is_ok());
    ASSERT(bm.is_bookmarked("https://example.com"));
    ASSERT(!bm.is_bookmarked("https://other.com"));
})

TEST(bookmark_remove, {
    BookmarkManager bm;
    bm.add("https://example.com", "Example");
    bm.remove("https://example.com");
    ASSERT(!bm.is_bookmarked("https://example.com"));
})

TEST(bookmark_remove_nonexistent, {
    BookmarkManager bm;
    auto r = bm.remove("https://example.com");
    ASSERT(r.is_ok());  // removing non-existent is a no-op success
})

TEST(bookmark_all, {
    BookmarkManager bm;
    bm.add("https://a.com", "A");
    bm.add("https://b.com", "B");
    auto all = bm.all();
    ASSERT(all.size() == 2);
})

TEST(bookmark_save_load, {
    BookmarkManager bm;
    bm.add("https://example.com", "Example");
    auto r = bm.save_to_file("test_bookmarks.txt");
    ASSERT(r.is_ok());

    BookmarkManager bm2;
    auto r2 = bm2.load_from_file("test_bookmarks.txt");
    ASSERT(r2.is_ok());
    ASSERT(bm2.is_bookmarked("https://example.com"));
    ASSERT(bm2.all().size() == 1);
    ASSERT(bm2.all()[0].url == "https://example.com");

    std::remove("test_bookmarks.txt");
})

TEST(bookmark_load_missing_file, {
    BookmarkManager bm;
    auto r = bm.load_from_file("nonexistent_bookmarks.txt");
    ASSERT(r.is_err());  // error on missing file
})

TEST(bookmark_add_twice, {
    BookmarkManager bm;
    bm.add("https://example.com", "A");
    bm.add("https://example.com", "B");
    auto all = bm.all();
    ASSERT(all.size() == 1);  // still only one entry
})

TEST(bookmark_timestamp, {
    BookmarkManager bm;
    bm.add("https://example.com", "Example");
    auto all = bm.all();
    ASSERT(all[0].added_at > 0);  // timestamp should be set
})

} // namespace browser
