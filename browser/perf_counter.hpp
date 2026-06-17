#pragma once
#include "../js/vm/gc.hpp"
#include "../tests/utility.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace browser {

    struct PerfCounters {
        // Frame timing (set each frame by BrowserWindow)
        std::atomic<u64> frame_count{0};
        std::atomic<f32> current_fps{0};
        std::atomic<f32> min_fps{999};
        std::atomic<f32> max_fps{0};
        std::atomic<f32> avg_fps{0};
        std::atomic<f32> frame_time_ms{0};
        std::atomic<f32> events_time_ms{0};
        std::atomic<f32> layout_time_ms{0};
        std::atomic<f32> paint_time_ms{0};
        std::atomic<f32> composite_time_ms{0};
        std::atomic<f32> gpu_time_ms{0};
        f32 fps_history[120] = {};
        u32 fps_history_idx = 0;

        // Memory
        std::atomic<u64> working_set_bytes{0};
        std::atomic<u64> heap_allocated_bytes{0};
        std::atomic<u64> heap_alloc_count{0};

        // Cache stats
        std::atomic<u64> cache_hits{0};
        std::atomic<u64> cache_misses{0};
        std::atomic<u64> cache_size_bytes{0};
        std::atomic<u32> cache_entry_count{0};

        // GC stats
        std::atomic<u32> gc_cycle_count{0};
        std::atomic<u32> gc_live_objects{0};
        std::atomic<u32> gc_live_functions{0};
        std::atomic<u64> gc_heap_size{0};
        std::atomic<u32> gc_collected_last{0};
        std::atomic<f32> gc_pause_time_ms{0};

        // Resource loading
        struct ResourceTiming {
            std::string url;
            std::string type;
            u64 start_ms;
            u64 end_ms;
            u64 byte_size;
        };
        std::vector<ResourceTiming> resources;

        // Aggregate counters
        std::atomic<u32> total_resources{0};
        std::atomic<u32> total_bytes{0};

        static PerfCounters &instance();

        void record_frame(f32 dt_ms, f32 events_ms, f32 layout_ms, f32 paint_ms, f32 composite_ms, f32 gpu_ms);

        void record_cache_hit() { cache_hits++; }
        void record_cache_miss() { cache_misses++; }

        void refresh_gc_stats(const js::GCHeap *heap);

        void reset();
    };

}  // namespace browser
