#pragma once
#include "../tests/utility.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace browser::net {

class Storage {
public:
    Storage() = default;
    explicit Storage(const std::string& origin);

    Result<void> set_item(const std::string& key, const std::string& value);
    std::optional<std::string> get_item(const std::string& key) const;
    void remove_item(const std::string& key);
    void clear();
    size_t length() const;
    std::string key(size_t index) const;

    Result<void> load_from_file(const std::string& path);
    Result<void> save_to_file(const std::string& path);

    static Storage& local_storage(const std::string& origin);
    static Storage& session_storage(const std::string& origin);
    static void flush_all();

    const std::string& origin() const { return origin_; }

    static std::string origin_hash(const std::string& origin);

private:
    std::unordered_map<std::string, std::string> data_;
    std::vector<std::string> insertion_order_;
    std::string origin_;
    size_t total_size_ = 0;

    static constexpr size_t kMaxSize = 5 * 1024 * 1024;
};

} // namespace browser::net
