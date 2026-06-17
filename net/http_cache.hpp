#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "http.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace browser::net::cache {

struct CacheEntry {
    std::string cache_key;
    u16 status_code;
    std::string http_version;
    http::Headers headers;
    std::vector<u8> body;
    u64 stored_time;      // when stored (ms since epoch)
    u64 max_age_secs;     // from Cache-Control
    std::string etag;
    std::string last_modified;
};

class HTTPCache {
public:
    HTTPCache();
    ~HTTPCache();

    HTTPCache(const HTTPCache&) = delete;
    HTTPCache& operator=(const HTTPCache&) = delete;

    // Initialize cache (creates directory, loads index)
    Result<void> init(const std::string& cache_dir = "./cache/");

    // Look up a response in cache
    Result<CacheEntry> lookup(const std::string& method, const std::string& url);

    // Store a response in cache
    Result<void> store(const std::string& method, const std::string& url,
                       const http::Response& response);

    // Check if a cached entry is fresh (not expired)
    static bool is_fresh(const CacheEntry& entry);

    // Build conditional request headers for revalidation
    static void add_conditional_headers(const CacheEntry& entry, http::Request& req);

    // Update entry from 304 response
    Result<void> update_from_304(CacheEntry& entry, const http::Response& response);

    // Clear cache
    Result<void> clear();

    // Stats
    u64 total_size() const { return total_cache_size_; }
    u32 entry_count() const { return static_cast<u32>(index_.size()); }
    u64 hit_count() const { return hit_count_; }
    u64 miss_count() const { return miss_count_; }

private:
    std::string cache_dir_;
    std::string responses_dir_;
    std::unordered_map<std::string, CacheEntry> index_;
    u64 total_cache_size_ = 0;
    u64 hit_count_ = 0;
    u64 miss_count_ = 0;

    static constexpr u64 kMaxCacheSize = 500 * 1024 * 1024;

    std::string entry_path(const std::string& key) const;
    std::string index_path() const;

    Result<void> save_index();
    Result<void> load_index();
    Result<void> write_body_file(const std::string& key, const std::vector<u8>& body);
    Result<std::vector<u8>> read_body_file(const std::string& key) const;
    void evict_lru();

    static std::string make_key(const std::string& method, const std::string& url);
};

} // namespace browser::net::cache
