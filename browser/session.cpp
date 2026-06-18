#include "session.hpp"

#include <cstring>
#include <fstream>

namespace browser {

    SessionManager::SessionManager() = default;

    void SessionManager::save(const std::vector<SessionEntry> &tabs) {
        // Mark dirty before starting the write; will set to "ok" on success
        mark_dirty();

        std::ofstream f(path_, std::ios::binary);
        if (!f.is_open())
            return;

        u32 version = 1;
        u32 count = static_cast<u32>(tabs.size());
        f.write(reinterpret_cast<const char *>(&version), sizeof(version));
        f.write(reinterpret_cast<const char *>(&count), sizeof(count));

        for (auto &entry : tabs) {
            u32 url_len = static_cast<u32>(entry.url.size());
            u32 title_len = static_cast<u32>(entry.title.size());
            f.write(reinterpret_cast<const char *>(&url_len), sizeof(url_len));
            f.write(entry.url.data(), url_len);
            f.write(reinterpret_cast<const char *>(&title_len), sizeof(title_len));
            f.write(entry.title.data(), title_len);
            f.write(reinterpret_cast<const char *>(&entry.scroll_y), sizeof(entry.scroll_y));
        }

        // Write lock file to indicate clean session
        std::ofstream lock(lock_path_, std::ios::binary);
        if (lock.is_open()) {
            lock.write("ok", 2);
        }
    }

    void SessionManager::mark_dirty() {
        std::ofstream lock(lock_path_, std::ios::binary);
        if (lock.is_open()) {
            lock.write("dirty", 5);
        }
    }

    std::vector<SessionEntry> SessionManager::load() {
        std::vector<SessionEntry> result;
        std::ifstream f(path_, std::ios::binary);
        if (!f.is_open())
            return result;

        u32 version = 0;
        f.read(reinterpret_cast<char *>(&version), sizeof(version));
        if (version != 1)
            return result;

        u32 count = 0;
        f.read(reinterpret_cast<char *>(&count), sizeof(count));
        if (!f)
            return result;

        for (u32 i = 0; i < count; i++) {
            SessionEntry entry;
            u32 url_len = 0, title_len = 0;
            f.read(reinterpret_cast<char *>(&url_len), sizeof(url_len));
            if (!f)
                break;
            if (url_len > 65536)
                break;
            entry.url.resize(url_len);
            f.read(&entry.url[0], url_len);
            if (!f)
                break;
            f.read(reinterpret_cast<char *>(&title_len), sizeof(title_len));
            if (!f)
                break;
            if (title_len > 65536)
                break;
            entry.title.resize(title_len);
            f.read(&entry.title[0], title_len);
            if (!f)
                break;
            f.read(reinterpret_cast<char *>(&entry.scroll_y), sizeof(entry.scroll_y));
            if (!f)
                break;
            result.push_back(std::move(entry));
        }
        return result;
    }

    bool SessionManager::previous_session_crashed() const {
        std::ifstream f(lock_path_);
        if (!f.is_open())
            return false;
        std::string content;
        std::getline(f, content);
        return content != "ok";
    }

    void SessionManager::clear() {
        std::remove(path_.c_str());
        std::remove(lock_path_.c_str());
    }

}  // namespace browser
