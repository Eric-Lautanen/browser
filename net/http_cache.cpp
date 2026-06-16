#include "http_cache.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace browser::net::cache {

static u64 now_ms() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

HTTPCache::HTTPCache() = default;
HTTPCache::~HTTPCache() = default;

std::string HTTPCache::make_key(const std::string& method, const std::string& url) {
    std::string key = method + ":" + url;
    // Sanitize for filename
    for (auto& c : key) {
        if (c == '/' || c == '\\' || c == ':' || c == '?' || c == '&' || c == '#' || c == '<' || c == '>' || c == '"')
            c = '_';
    }
    return key;
}

std::string HTTPCache::entry_path(const std::string& key) const {
    return cache_dir_ + key + ".resp";
}

std::string HTTPCache::index_path() const {
    return cache_dir_ + "index.dat";
}

Result<void> HTTPCache::init(const std::string& cache_dir) {
    cache_dir_ = cache_dir;
    if (cache_dir_.back() != '/' && cache_dir_.back() != '\\')
        cache_dir_ += '/';

    // Create directory if needed
    DWORD attrs = GetFileAttributesA(cache_dir_.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(cache_dir_.c_str(), nullptr))
            return std::string("failed to create cache directory");
    }

    // Load index
    return load_index();
}

Result<void> HTTPCache::save_index() {
    std::ofstream file(index_path(), std::ios::binary);
    if (!file) return std::string("failed to write index");

    u32 count = static_cast<u32>(index_.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (auto& [key, entry] : index_) {
        // Write key
        u32 key_len = static_cast<u32>(key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        file.write(key.data(), key_len);

        // Write entry fields
        file.write(reinterpret_cast<const char*>(&entry.status_code), sizeof(entry.status_code));

        u32 ver_len = static_cast<u32>(entry.http_version.size());
        file.write(reinterpret_cast<const char*>(&ver_len), sizeof(ver_len));
        file.write(entry.http_version.data(), ver_len);

        u32 body_len = static_cast<u32>(entry.body.size());
        file.write(reinterpret_cast<const char*>(&body_len), sizeof(body_len));
        file.write(reinterpret_cast<const char*>(entry.body.data()), body_len);

        file.write(reinterpret_cast<const char*>(&entry.stored_time), sizeof(entry.stored_time));
        file.write(reinterpret_cast<const char*>(&entry.max_age_secs), sizeof(entry.max_age_secs));

        u32 etag_len = static_cast<u32>(entry.etag.size());
        file.write(reinterpret_cast<const char*>(&etag_len), sizeof(etag_len));
        file.write(entry.etag.data(), etag_len);

        u32 lm_len = static_cast<u32>(entry.last_modified.size());
        file.write(reinterpret_cast<const char*>(&lm_len), sizeof(lm_len));
        file.write(entry.last_modified.data(), lm_len);

        // Write headers
        u32 hdr_count = 0;
        std::string hdr_data;
        for (auto& [hk, hv] : entry.headers.all()) {
            u32 nk = static_cast<u32>(hk.size());
            u32 nv = static_cast<u32>(hv.size());
            hdr_data.append(reinterpret_cast<const char*>(&nk), sizeof(nk));
            hdr_data.append(hk.data(), nk);
            hdr_data.append(reinterpret_cast<const char*>(&nv), sizeof(nv));
            hdr_data.append(hv.data(), nv);
            hdr_count++;
        }
        file.write(reinterpret_cast<const char*>(&hdr_count), sizeof(hdr_count));
        u32 hdr_data_len = static_cast<u32>(hdr_data.size());
        file.write(reinterpret_cast<const char*>(&hdr_data_len), sizeof(hdr_data_len));
        if (hdr_data_len > 0)
            file.write(hdr_data.data(), hdr_data_len);
    }

    return {};
}

Result<void> HTTPCache::load_index() {
    index_.clear();
    std::ifstream file(index_path(), std::ios::binary);
    if (!file) return {}; // No index yet, not an error

    u32 count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!file) return {};

    for (u32 i = 0; i < count; i++) {
        u32 key_len = 0;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (!file) break;
        std::string key(key_len, '\0');
        file.read(&key[0], key_len);
        if (!file) break;

        CacheEntry entry;
        entry.cache_key = key;

        file.read(reinterpret_cast<char*>(&entry.status_code), sizeof(entry.status_code));

        u32 ver_len = 0;
        file.read(reinterpret_cast<char*>(&ver_len), sizeof(ver_len));
        if (!file) break;
        entry.http_version.resize(ver_len);
        file.read(&entry.http_version[0], ver_len);

        u32 body_len = 0;
        file.read(reinterpret_cast<char*>(&body_len), sizeof(body_len));
        if (!file) break;
        entry.body.resize(body_len);
        if (body_len > 0)
            file.read(reinterpret_cast<char*>(entry.body.data()), body_len);

        file.read(reinterpret_cast<char*>(&entry.stored_time), sizeof(entry.stored_time));
        file.read(reinterpret_cast<char*>(&entry.max_age_secs), sizeof(entry.max_age_secs));

        u32 etag_len = 0;
        file.read(reinterpret_cast<char*>(&etag_len), sizeof(etag_len));
        if (!file) break;
        entry.etag.resize(etag_len);
        file.read(&entry.etag[0], etag_len);

        u32 lm_len = 0;
        file.read(reinterpret_cast<char*>(&lm_len), sizeof(lm_len));
        if (!file) break;
        entry.last_modified.resize(lm_len);
        file.read(&entry.last_modified[0], lm_len);

        // Read headers
        u32 hdr_count = 0;
        file.read(reinterpret_cast<char*>(&hdr_count), sizeof(hdr_count));
        if (!file) break;
        u32 hdr_data_len = 0;
        file.read(reinterpret_cast<char*>(&hdr_data_len), sizeof(hdr_data_len));
        if (!file) break;
        std::string hdr_data;
        if (hdr_data_len > 0) {
            hdr_data.resize(hdr_data_len);
            file.read(&hdr_data[0], hdr_data_len);
        }

        u32 hdr_pos = 0;
        for (u32 j = 0; j < hdr_count; j++) {
            u32 nk = 0, nv = 0;
            if (hdr_pos + sizeof(nk) > hdr_data.size()) break;
            std::memcpy(&nk, hdr_data.data() + hdr_pos, sizeof(nk));
            hdr_pos += sizeof(nk);
            if (hdr_pos + nk > hdr_data.size()) break;
            std::string hk(hdr_data.data() + hdr_pos, nk);
            hdr_pos += nk;
            if (hdr_pos + sizeof(nv) > hdr_data.size()) break;
            std::memcpy(&nv, hdr_data.data() + hdr_pos, sizeof(nv));
            hdr_pos += sizeof(nv);
            if (hdr_pos + nv > hdr_data.size()) break;
            std::string hv(hdr_data.data() + hdr_pos, nv);
            hdr_pos += nv;
            entry.headers.set(hk, hv);
        }

        index_[key] = std::move(entry);
    }

    return {};
}

Result<CacheEntry> HTTPCache::lookup(const std::string& method, const std::string& url) {
    auto key = make_key(method, url);
    auto it = index_.find(key);
    if (it == index_.end()) {
        return std::string("not in cache");
    }
    return it->second;
}

Result<void> HTTPCache::store(const std::string& method, const std::string& url,
                               const http::Response& response) {
    auto key = make_key(method, url);

    CacheEntry entry;
    entry.cache_key = key;
    entry.status_code = response.status.code;
    entry.http_version = response.http_version;
    entry.headers = response.headers;
    entry.body = response.body;
    entry.stored_time = now_ms();

    // Extract cache control info
    std::string cc = response.headers.get("cache-control");
    if (!cc.empty()) {
        // Parse max-age
        auto pos = cc.find("max-age=");
        if (pos != std::string::npos) {
            pos += 8;
            auto end = cc.find_first_not_of("0123456789", pos);
            if (end == std::string::npos) end = cc.size();
            entry.max_age_secs = static_cast<u64>(std::stoul(cc.substr(pos, end - pos)));
        }
    }

    entry.etag = response.headers.get("etag");
    entry.last_modified = response.headers.get("last-modified");

    // Default cache time if no max-age AND no explicit Cache-Control: 10 minutes for successful responses
    if (entry.max_age_secs == 0 && !response.headers.has("cache-control") && response.status.code == 200) {
        entry.max_age_secs = 600;
    }

    index_[key] = std::move(entry);

    // Persist index
    return save_index();
}

bool HTTPCache::is_fresh(const CacheEntry& entry) {
    if (entry.max_age_secs == 0) return false;
    u64 age = (now_ms() - entry.stored_time) / 1000;
    return age < entry.max_age_secs;
}

void HTTPCache::add_conditional_headers(const CacheEntry& entry, http::Request& req) {
    if (!entry.etag.empty()) {
        req.headers.set("If-None-Match", entry.etag);
    }
    if (!entry.last_modified.empty()) {
        req.headers.set("If-Modified-Since", entry.last_modified);
    }
}

Result<void> HTTPCache::update_from_304(CacheEntry& entry, const http::Response& response) {
    // Update headers from 304 response
    for (auto& [k, v] : response.headers.all()) {
        entry.headers.set(k, v);
    }
    entry.stored_time = now_ms();

    // Update cache control
    std::string cc = response.headers.get("cache-control");
    if (!cc.empty()) {
        auto pos = cc.find("max-age=");
        if (pos != std::string::npos && pos + 8 <= cc.size()) {
            auto end = cc.find_first_not_of("0123456789", pos + 8);
            if (end == std::string::npos) end = cc.size();
            entry.max_age_secs = static_cast<u64>(std::stoul(cc.substr(pos + 8, end - pos - 8)));
        }
    }
    if (!response.headers.get("etag").empty())
        entry.etag = response.headers.get("etag");
    if (!response.headers.get("last-modified").empty())
        entry.last_modified = response.headers.get("last-modified");

    index_[entry.cache_key] = entry;
    return save_index();
}

Result<void> HTTPCache::clear() {
    index_.clear();
    return save_index();
}

} // namespace browser::net::cache
