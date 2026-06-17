#pragma once
#include "layer_tree.hpp"
#include "rasterizer.hpp"
#include "tile_cache.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <windows.h>

namespace browser::render {

    struct CompositedFrame {
        u32 width;
        u32 height;
        std::vector<u8> rgba_pixels;
        u64 frame_id = 0;
    };

    class Compositor {
    public:
        Compositor();
        ~Compositor();

        void start();
        void stop();

        // Main thread methods
        void commit_layer_tree(std::unique_ptr<LayerTree> tree);
        void set_root_scroll_delta(f32 dy);
        bool has_new_frame() const;
        CompositedFrame acquire_frame();
        void set_viewport(i32 w, i32 h);

        // Event handle for MsgWaitForMultipleObjectsEx
        HANDLE frame_ready_event() const { return frame_ready_event_; }

    private:
        void thread_func();

        void rasterize_visible_tiles(Layer *layer, f32 parent_scroll_x, f32 parent_scroll_y);
        void compose_layer(CompositedFrame &frame, Layer *layer, f32 parent_scroll_x, f32 parent_scroll_y);
        CompositedFrame compose_frame();

        std::atomic<bool> running_{false};
        std::thread thread_;

        // Thread-safe channels
        mutable CRITICAL_SECTION tree_mutex_;
        std::unique_ptr<LayerTree> pending_tree_;
        bool has_pending_tree_ = false;
        f32 pending_scroll_delta_ = 0;

        mutable CRITICAL_SECTION frame_mutex_;
        CompositedFrame pending_frame_;
        bool has_pending_frame_ = false;

        HANDLE work_event_;
        HANDLE frame_ready_event_;

        // Compositor state
        std::unique_ptr<LayerTree> current_tree_;
        TileCache tile_cache_;
        std::atomic<i32> viewport_width_{1024};
        std::atomic<i32> viewport_height_{768};
        u64 frame_id_counter_ = 0;

        static constexpr u32 TILE_SIZE = Rasterizer::TILE_SIZE;
    };

}  // namespace browser::render
