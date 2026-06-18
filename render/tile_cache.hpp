#pragma once
#include "rasterizer.hpp"

#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace browser::render {

    struct TileKey {
        u32 layer_id;
        i32 tile_x;
        i32 tile_y;
        f32 scale;

        bool operator==(const TileKey &o) const {
            return layer_id == o.layer_id && tile_x == o.tile_x && tile_y == o.tile_y && scale == o.scale;
        }
    };

    struct TileKeyHash {
        size_t operator()(const TileKey &k) const {
            size_t h = static_cast<size_t>(k.layer_id);
            h = h * 31 + static_cast<size_t>(k.tile_x);
            h = h * 31 + static_cast<size_t>(k.tile_y);
            h = h * 31 + static_cast<size_t>(k.scale * 1000);
            return h;
        }
    };

    struct CacheEntry {
        RasterizedTile tile;
        std::list<TileKey>::iterator lru_it;
    };

    class TileCache {
    public:
        static constexpr size_t MAX_CACHE_BYTES = 256 * 1024 * 1024;  // 256MB

        std::optional<RasterizedTile> lookup(TileKey key);
        void insert(TileKey key, RasterizedTile tile);
        void evict_oldest();
        void clear_layer(u32 layer_id);
        void clear();

        size_t total_bytes() const { return total_cache_bytes_; }
        size_t eviction_count() const { return eviction_count_; }

    private:
        std::unordered_map<TileKey, CacheEntry, TileKeyHash> cache_;
        std::list<TileKey> lru_list_;
        size_t total_cache_bytes_ = 0;
        size_t eviction_count_ = 0;
    };

}  // namespace browser::render
