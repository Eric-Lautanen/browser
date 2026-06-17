#include "tile_cache.hpp"

#include <algorithm>

namespace browser::render {

    std::optional<RasterizedTile> TileCache::lookup(TileKey key) {
        auto it = cache_.find(key);
        if (it == cache_.end())
            return std::nullopt;
        touch_key(key);
        return it->second;
    }

    void TileCache::insert(TileKey key, RasterizedTile tile) {
        size_t tile_bytes = tile.rgba_pixels.size();
        while (total_cache_bytes_ + tile_bytes > MAX_CACHE_BYTES && !cache_.empty()) {
            evict_oldest();
        }
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            total_cache_bytes_ -= it->second.rgba_pixels.size();
            it->second = std::move(tile);
            total_cache_bytes_ += it->second.rgba_pixels.size();
            touch_key(key);
        } else {
            total_cache_bytes_ += tile_bytes;
            cache_[key] = std::move(tile);
            lru_list_.push_front(key);
        }
    }

    void TileCache::evict_oldest() {
        if (lru_list_.empty())
            return;
        TileKey key = lru_list_.back();
        lru_list_.pop_back();
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            total_cache_bytes_ -= it->second.rgba_pixels.size();
            cache_.erase(it);
            eviction_count_++;
        }
    }

    void TileCache::clear_layer(u32 layer_id) {
        std::vector<TileKey> to_remove;
        for (auto &[key, _] : cache_) {
            if (key.layer_id == layer_id) {
                to_remove.push_back(key);
            }
        }
        for (auto &key : to_remove) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                total_cache_bytes_ -= it->second.rgba_pixels.size();
                cache_.erase(it);
            }
            lru_list_.remove(key);
        }
    }

    void TileCache::clear() {
        cache_.clear();
        lru_list_.clear();
        total_cache_bytes_ = 0;
        eviction_count_ = 0;
    }

    void TileCache::touch_key(const TileKey &key) {
        lru_list_.remove(key);
        lru_list_.push_front(key);
    }

}  // namespace browser::render
