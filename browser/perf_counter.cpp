#include "perf_counter.hpp"

#include "../async/memory.hpp"

#include <psapi.h>

namespace browser {

    PerfCounters &PerfCounters::instance() {
        static PerfCounters inst;
        return inst;
    }

    void PerfCounters::record_frame(
        f32 dt_ms, f32 events_ms, f32 layout_ms, f32 paint_ms, f32 composite_ms, f32 gpu_ms) {
        frame_count++;
        frame_time_ms.store(dt_ms, std::memory_order_relaxed);
        events_time_ms.store(events_ms, std::memory_order_relaxed);
        layout_time_ms.store(layout_ms, std::memory_order_relaxed);
        paint_time_ms.store(paint_ms, std::memory_order_relaxed);
        composite_time_ms.store(composite_ms, std::memory_order_relaxed);
        gpu_time_ms.store(gpu_ms, std::memory_order_relaxed);

        if (dt_ms > 0.0f) {
            f32 fps = 1000.0f / dt_ms;
            current_fps.store(fps, std::memory_order_relaxed);
            if (fps < min_fps.load(std::memory_order_relaxed))
                min_fps.store(fps, std::memory_order_relaxed);
            if (fps > max_fps.load(std::memory_order_relaxed))
                max_fps.store(fps, std::memory_order_relaxed);
            f32 old_avg = avg_fps.load(std::memory_order_relaxed);
            u64 count = frame_count.load(std::memory_order_relaxed);
            avg_fps.store(old_avg + (fps - old_avg) / (f32)count, std::memory_order_relaxed);
        }

        // Update FPS history ring buffer
        fps_history[fps_history_idx % 120] = current_fps.load(std::memory_order_relaxed);
        fps_history_idx = (fps_history_idx + 1) % 120;

        // Live memory pull from Mallocator
        heap_allocated_bytes.store((u64)async::Mallocator::instance().total_allocated(), std::memory_order_relaxed);
        heap_alloc_count.store((u64)async::Mallocator::instance().alloc_count(), std::memory_order_relaxed);

        // Working set via GetProcessWorkingSetSize
        HANDLE proc = GetCurrentProcess();
        SIZE_T min_ws = 0, max_ws = 0;
        if (GetProcessWorkingSetSize(proc, &min_ws, &max_ws)) {
            (void)min_ws;
            (void)max_ws;
        }
        PROCESS_MEMORY_COUNTERS pmc;
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
            working_set_bytes.store((u64)pmc.WorkingSetSize, std::memory_order_relaxed);
        }
    }

    void PerfCounters::refresh_gc_stats(const js::GCHeap *heap) {
        if (!heap)
            return;
        gc_cycle_count.store(heap->cycle_count(), std::memory_order_relaxed);
        gc_live_objects.store(heap->object_count(), std::memory_order_relaxed);
        gc_live_functions.store(heap->function_count(), std::memory_order_relaxed);
        gc_heap_size.store((u64)heap->allocated_bytes(), std::memory_order_relaxed);
        gc_collected_last.store(heap->last_collected(), std::memory_order_relaxed);
        gc_pause_time_ms.store(heap->last_pause_ms(), std::memory_order_relaxed);
    }

    void PerfCounters::reset() {
        frame_count = 0;
        current_fps = 0;
        min_fps = 999;
        max_fps = 0;
        avg_fps = 0;
        frame_time_ms = 0;
        events_time_ms = 0;
        layout_time_ms = 0;
        paint_time_ms = 0;
        composite_time_ms = 0;
        gpu_time_ms = 0;
        fps_history_idx = 0;
        for (auto &v : fps_history) v = 0;

        working_set_bytes = 0;
        heap_allocated_bytes = 0;
        heap_alloc_count = 0;

        cache_hits = 0;
        cache_misses = 0;
        cache_size_bytes = 0;
        cache_entry_count = 0;

        gc_cycle_count = 0;
        gc_live_objects = 0;
        gc_live_functions = 0;
        gc_heap_size = 0;
        gc_collected_last = 0;
        gc_pause_time_ms = 0;

        resources.clear();
        total_resources = 0;
        total_bytes = 0;
    }

}  // namespace browser
