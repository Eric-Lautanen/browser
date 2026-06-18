#include "tile_cache.hpp"

#include <algorithm>

namespace browser::render {

    std::optional<RasterizedTile> TileCache::lookup(TileKey key) {
        auto it = cache_.find(key);
        if (it == cache_.end())
            return std::nullopt;
        // Move to front of LRU list
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
        return it->second.tile;
    }

    void TileCache::insert(TileKey key, RasterizedTile tile) {
        size_t tile_bytes = tile.rgba_pixels.size();
        while (total_cache_bytes_ + tile_bytes > MAX_CACHE_BYTES && !cache_.empty()) {
            evict_oldest();
        }
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            total_cache_bytes_ -= it->second.tile.rgba_pixels.size();
            it->second.tile = std::move(tile);
            total_cache_bytes_ += it->second.tile.rgba_pixels.size();
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second.lru_it);
        } else {
            total_cache_bytes_ += tile_bytes;
            lru_list_.push_front(key);
            CacheEntry entry;
            entry.tile = std::move(tile);
            entry.lru_it = lru_list_.begin();
            cache_[key] = std::move(entry);
        }
    }

    void TileCache::evict_oldest() {
        if (lru_list_.empty())
            return;
        TileKey key = lru_list_.back();
        lru_list_.pop_back();
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            total_cache_bytes_ -= it->second.tile.rgba_pixels.size();
            cache_.erase(it);
            eviction_count_++;
        }
    }

    void TileCache::clear_layer(u32 layer_id) {
        std::vector<TileKey> to_remove;
        for (auto &[key, entry] : cache_) {
            if (key.layer_id == layer_id) {
                to_remove.push_back(key);
            }
        }
        for (auto &key : to_remove) {
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                total_cache_bytes_ -= it->second.tile.rgba_pixels.size();
                lru_list_.erase(it->second.lru_it);
                cache_.erase(it);
            }
        }
    }

    void TileCache::clear() {
        cache_.clear();
        lru_list_.clear();
        total_cache_bytes_ = 0;
        eviction_count_ = 0;
    }

}  // namespace browser::render
