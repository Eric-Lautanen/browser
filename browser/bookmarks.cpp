#include "bookmarks.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace browser {

    std::string BookmarkManager::default_path() {
        char buf[MAX_PATH];
        DWORD len = GetEnvironmentVariableA("USERPROFILE", buf, sizeof(buf));
        if (len > 0 && len < sizeof(buf)) {
            std::string dir = std::string(buf) + "\\Documents";
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir + "\\browser_bookmarks.txt";
        }
        return "bookmarks.txt";
    }

    BookmarkManager::BookmarkManager() {}

    Result<void> BookmarkManager::add(const std::string &url, const std::string &title) {
        if (is_bookmarked(url))
            return {};
        auto now = std::chrono::system_clock::now();
        u64 ts = static_cast<u64>(now.time_since_epoch().count());
        bookmarks_.push_back({url, title, ts, ""});
        return {};
    }

    Result<void> BookmarkManager::remove(const std::string &url) {
        for (auto it = bookmarks_.begin(); it != bookmarks_.end(); ++it) {
            if (it->url == url) {
                bookmarks_.erase(it);
                return {};
            }
        }
        return {};
    }

    bool BookmarkManager::is_bookmarked(const std::string &url) const {
        for (const auto &b : bookmarks_) {
            if (b.url == url)
                return true;
        }
        return false;
    }

    std::vector<Bookmark> BookmarkManager::all() const {
        return bookmarks_;
    }

    static std::string escape_pipe(const std::string &s) {
        std::string r;
        for (char c : s) {
            if (c == '|')
                r += "\\p";
            else
                r += c;
        }
        return r;
    }

    static std::string unescape_pipe(const std::string &s) {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'p') {
                r += '|';
                i++;
            } else {
                r += s[i];
            }
        }
        return r;
    }

    Result<void> BookmarkManager::save_to_file(const std::string &path) {
        std::ofstream file(path);
        if (!file.is_open())
            return std::string("Failed to open file for writing: " + path);
        for (const auto &b : bookmarks_) {
            file << escape_pipe(b.title) << '|' << escape_pipe(b.url) << '|' << b.added_at << '|'
                 << escape_pipe(b.folder) << '\n';
        }
        file.close();
        if (!file.good())
            return std::string("Failed to write file: " + path);
        return {};
    }

    Result<void> BookmarkManager::load_from_file(const std::string &path) {
        std::ifstream file(path);
        if (!file.is_open())
            return std::string("Failed to open file for reading: " + path);
        bookmarks_.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty())
                continue;
            std::vector<std::string> parts;
            size_t start = 0, end;
            while ((end = line.find('|', start)) != std::string::npos) {
                parts.push_back(line.substr(start, end - start));
                start = end + 1;
            }
            parts.push_back(line.substr(start));
            if (parts.size() < 3)
                continue;
            Bookmark b;
            if (parts.size() > 4) {
                b.title = unescape_pipe(parts[0]);
                for (size_t i = 1; i < parts.size() - 3; i++) b.title += "|" + unescape_pipe(parts[i]);
                b.url = unescape_pipe(parts[parts.size() - 3]);
                char *endp = nullptr;
                b.added_at = static_cast<u64>(std::strtoull(parts[parts.size() - 2].c_str(), &endp, 10));
                if (endp == parts[parts.size() - 2].c_str())
                    b.added_at = 0;
                b.folder = parts[parts.size() - 1];
            } else {
                b.title = unescape_pipe(parts[0]);
                b.url = unescape_pipe(parts[1]);
                char *endp = nullptr;
                b.added_at = static_cast<u64>(std::strtoull(parts[2].c_str(), &endp, 10));
                if (endp == parts[2].c_str())
                    b.added_at = 0;
                if (parts.size() >= 4)
                    b.folder = parts[3];
            }
            bookmarks_.push_back(b);
        }
        return {};
    }

}  // namespace browser
