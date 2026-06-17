#pragma once
#include "../tests/utility.hpp"

#include <string>
#include <vector>

namespace browser {

    struct Bookmark {
        std::string url;
        std::string title;
        u64 added_at;
        std::string folder;
    };

    class BookmarkManager {
    public:
        BookmarkManager();
        Result<void> add(const std::string &url, const std::string &title);
        Result<void> remove(const std::string &url);
        bool is_bookmarked(const std::string &url) const;
        std::vector<Bookmark> all() const;
        static std::string default_path();
        Result<void> save_to_file(const std::string &path);
        Result<void> load_from_file(const std::string &path);

    private:
        std::vector<Bookmark> bookmarks_;
    };

}  // namespace browser
