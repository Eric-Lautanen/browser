#include "storage.hpp"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace browser::net {

static std::unordered_map<std::string, std::unique_ptr<Storage>> g_local_storage;
static std::unordered_map<std::string, std::unique_ptr<Storage>> g_session_storage;
static std::mutex g_storage_mutex;

std::string Storage::origin_hash(const std::string& origin) {
    // Simple FNV-1a hash for the origin string
    u64 hash = 14695981039346656037ULL;
    for (char c : origin) {
        hash ^= static_cast<u8>(c);
        hash *= 1099511628211ULL;
    }
    // Convert to hex string
    std::ostringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

Storage::Storage(const std::string& origin) : origin_(origin) {}

Result<void> Storage::set_item(const std::string& key, const std::string& value) {
    size_t old_size = 0;
    auto it = data_.find(key);
    if (it != data_.end()) {
        old_size = it->second.size();
    }

    size_t new_size = total_size_ - old_size + key.size() + value.size();
    if (new_size > kMaxSize) {
        return std::string("storage quota exceeded");
    }

    if (it == data_.end()) {
        insertion_order_.push_back(key);
    }

    data_[key] = value;
    total_size_ = new_size;
    return {};
}

std::optional<std::string> Storage::get_item(const std::string& key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void Storage::remove_item(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        total_size_ -= (key.size() + it->second.size());
        data_.erase(it);
        for (size_t i = 0; i < insertion_order_.size(); i++) {
            if (insertion_order_[i] == key) {
                insertion_order_.erase(insertion_order_.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
}

void Storage::clear() {
    data_.clear();
    insertion_order_.clear();
    total_size_ = 0;
}

size_t Storage::length() const {
    return data_.size();
}

std::string Storage::key(size_t index) const {
    if (index >= insertion_order_.size()) return {};
    return insertion_order_[index];
}

Result<void> Storage::load_from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};

    data_.clear();
    insertion_order_.clear();
    total_size_ = 0;

    u32 count = 0;
    f.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!f) return {};

    for (u32 i = 0; i < count; i++) {
        u32 key_len = 0;
        f.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (!f) break;
        std::string key(key_len, '\0');
        f.read(&key[0], key_len);
        if (!f) break;

        u32 val_len = 0;
        f.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        if (!f) break;
        std::string val(val_len, '\0');
        f.read(&val[0], val_len);
        if (!f) break;

        data_[key] = val;
        insertion_order_.push_back(key);
        total_size_ += key.size() + val.size();
    }

    return {};
}

Result<void> Storage::save_to_file(const std::string& path) {
    // Create directory if needed
    std::string dir;
    auto slash = path.rfind('/');
    if (slash == std::string::npos) slash = path.rfind('\\');
    if (slash != std::string::npos) {
        dir = path.substr(0, slash);
        DWORD attrs = GetFileAttributesA(dir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            if (!CreateDirectoryA(dir.c_str(), nullptr))
                return std::string("failed to create storage directory");
        }
    }

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return std::string("cannot open " + path + " for writing");

    u32 count = static_cast<u32>(data_.size());
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& key : insertion_order_) {
        auto it = data_.find(key);
        if (it == data_.end()) continue;

        u32 key_len = static_cast<u32>(key.size());
        f.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        f.write(key.data(), key_len);

        u32 val_len = static_cast<u32>(it->second.size());
        f.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        f.write(it->second.data(), val_len);
    }

    if (!f.good()) return std::string("write error");
    return {};
}

Storage& Storage::local_storage(const std::string& origin) {
    std::lock_guard<std::mutex> lock(g_storage_mutex);
    auto it = g_local_storage.find(origin);
    if (it != g_local_storage.end()) {
        return *it->second;
    }
    auto storage = std::make_unique<Storage>(origin);
    auto* ptr = storage.get();
    g_local_storage[origin] = std::move(storage);

    // Try to load from file
    std::string path = "./storage/" + origin_hash(origin) + "/localstorage.dat";
    ptr->load_from_file(path);

    return *ptr;
}

Storage& Storage::session_storage(const std::string& origin) {
    std::lock_guard<std::mutex> lock(g_storage_mutex);
    auto it = g_session_storage.find(origin);
    if (it != g_session_storage.end()) {
        return *it->second;
    }
    auto storage = std::make_unique<Storage>(origin);
    auto* ptr = storage.get();
    g_session_storage[origin] = std::move(storage);
    return *ptr;
}

void Storage::flush_all() {
    std::lock_guard<std::mutex> lock(g_storage_mutex);
    for (auto& [origin, storage] : g_local_storage) {
        std::string path = "./storage/" + origin_hash(origin) + "/localstorage.dat";
        storage->save_to_file(path);
    }
}

} // namespace browser::net
