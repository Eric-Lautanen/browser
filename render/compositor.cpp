#include "compositor.hpp"

#include <algorithm>
#include <cmath>

namespace browser::render {

    Compositor::Compositor() {
        InitializeCriticalSection(&tree_mutex_);
        InitializeCriticalSection(&frame_mutex_);
        work_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        frame_ready_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    Compositor::~Compositor() {
        stop();
        DeleteCriticalSection(&tree_mutex_);
        DeleteCriticalSection(&frame_mutex_);
        if (work_event_)
            CloseHandle(work_event_);
        if (frame_ready_event_)
            CloseHandle(frame_ready_event_);
    }

    void Compositor::start() {
        if (running_.exchange(true))
            return;
        thread_ = std::thread(&Compositor::thread_func, this);
    }

    void Compositor::stop() {
        if (!running_.exchange(false))
            return;
        SetEvent(work_event_);
        if (thread_.joinable())
            thread_.join();
    }

    void Compositor::commit_layer_tree(std::unique_ptr<LayerTree> tree) {
        EnterCriticalSection(&tree_mutex_);
        pending_tree_ = std::move(tree);
        has_pending_tree_ = true;
        LeaveCriticalSection(&tree_mutex_);
        SetEvent(work_event_);
    }

    bool Compositor::has_new_frame() const {
        EnterCriticalSection(&frame_mutex_);
        bool has = has_pending_frame_;
        LeaveCriticalSection(&frame_mutex_);
        return has;
    }

    CompositedFrame Compositor::acquire_frame() {
        EnterCriticalSection(&frame_mutex_);
        CompositedFrame frame = std::move(pending_frame_);
        has_pending_frame_ = false;
        LeaveCriticalSection(&frame_mutex_);
        return frame;
    }

    void Compositor::set_viewport(i32 w, i32 h) {
        viewport_width_.store(w);
        viewport_height_.store(h);
        SetEvent(work_event_);
    }

    void Compositor::set_root_scroll_delta(f32 dy) {
        EnterCriticalSection(&tree_mutex_);
        pending_scroll_delta_ += dy;
        has_pending_tree_ = true;  // Trigger re-composite
        LeaveCriticalSection(&tree_mutex_);
        SetEvent(work_event_);
    }

    void Compositor::thread_func() {
        while (running_.load()) {
            WaitForSingleObject(work_event_, 100);

            // Check for a new tree and scroll updates
            EnterCriticalSection(&tree_mutex_);
            if (has_pending_tree_) {
                if (pending_tree_) {
                    // Clear tile cache on new tree to prevent stale tiles
                    tile_cache_.clear();
                    current_tree_ = std::move(pending_tree_);
                }
                if (pending_scroll_delta_ != 0 && current_tree_ && current_tree_->root_layer) {
                    current_tree_->root_layer->scroll_offset_y += pending_scroll_delta_;
                    if (current_tree_->root_layer->scroll_offset_y < 0)
                        current_tree_->root_layer->scroll_offset_y = 0;
                    pending_scroll_delta_ = 0;
                }
                has_pending_tree_ = false;
            }
            LeaveCriticalSection(&tree_mutex_);

            if (!current_tree_)
                continue;

            // Rasterize visible tiles for all layers
            for (auto *layer : current_tree_->all_layers) {
                if (layer->layout_node) {
                    rasterize_visible_tiles(layer, 0, 0);
                }
            }

            // Compose the frame
            CompositedFrame frame = compose_frame();

            // Signal the main thread
            EnterCriticalSection(&frame_mutex_);
            pending_frame_ = std::move(frame);
            has_pending_frame_ = true;
            LeaveCriticalSection(&frame_mutex_);
            SetEvent(frame_ready_event_);
        }
    }

    void Compositor::rasterize_visible_tiles(Layer *layer, f32 parent_scroll_x, f32 parent_scroll_y) {
        if (!layer || !layer->display_list)
            return;

        // Accumulate scroll offsets from parent layers
        f32 eff_scroll_x = parent_scroll_x + layer->scroll_offset_x;
        f32 eff_scroll_y = parent_scroll_y + layer->scroll_offset_y;

        // Determine visible tile range
        // The layer's content is positioned at (bounds.x, bounds.y) in document space
        // Scroll offset shifts what's visible
        f32 visible_left = -(layer->bounds.x - eff_scroll_x);
        f32 visible_top = -(layer->bounds.y - eff_scroll_y);
        f32 visible_right = visible_left + static_cast<f32>(viewport_width_);
        f32 visible_bottom = visible_top + static_cast<f32>(viewport_height_);

        i32 tile_start_x = static_cast<i32>(std::floor(visible_left / TILE_SIZE));
        i32 tile_start_y = static_cast<i32>(std::floor(visible_top / TILE_SIZE));
        i32 tile_end_x = static_cast<i32>(std::ceil(visible_right / TILE_SIZE));
        i32 tile_end_y = static_cast<i32>(std::ceil(visible_bottom / TILE_SIZE));

        for (i32 ty = tile_start_y; ty < tile_end_y; ty++) {
            for (i32 tx = tile_start_x; tx < tile_end_x; tx++) {
                TileKey key{layer->layer_id, tx, ty, 1.0f};
                auto cached = tile_cache_.lookup(key);
                if (cached.has_value())
                    continue;

                RasterizedTile tile =
                    Rasterizer::rasterize_tile(*layer, tx, ty, layer->scroll_offset_x, layer->scroll_offset_y);
                tile_cache_.insert(key, std::move(tile));
            }
        }

        // Process child layers recursively
        for (const auto &child : layer->children) {
            rasterize_visible_tiles(child.get(), eff_scroll_x, eff_scroll_y);
        }
    }

    CompositedFrame Compositor::compose_frame() {
        CompositedFrame frame;
        frame.width = static_cast<u32>(viewport_width_);
        frame.height = static_cast<u32>(viewport_height_);
        frame.frame_id = ++frame_id_counter_;
        frame.rgba_pixels.resize(static_cast<size_t>(viewport_width_) * static_cast<size_t>(viewport_height_) * 4, 0);

        if (!current_tree_ || !current_tree_->root_layer)
            return frame;

        // Compose layers: first root, then children (painter's algorithm)
        compose_layer(frame, current_tree_->root_layer.get(), 0, 0);

        return frame;
    }

    namespace {

        void blit_tile_to_frame(CompositedFrame &frame,
                                const RasterizedTile &tile,
                                f32 screen_x,
                                f32 screen_y,
                                f32 scroll_x,
                                f32 scroll_y) {
            // Tile position on screen
            i32 tile_screen_x = static_cast<i32>(screen_x + tile.tile_x * Rasterizer::TILE_SIZE - scroll_x);
            i32 tile_screen_y = static_cast<i32>(screen_y + tile.tile_y * Rasterizer::TILE_SIZE - scroll_y);
            i32 tile_w = static_cast<i32>(tile.width);
            i32 tile_h = static_cast<i32>(tile.height);

            i32 fw = static_cast<i32>(frame.width);
            i32 fh = static_cast<i32>(frame.height);

            // Clip to frame bounds
            i32 src_x = 0;
            i32 src_y = 0;
            if (tile_screen_x < 0) {
                src_x = -tile_screen_x;
                tile_w += tile_screen_x;
                tile_screen_x = 0;
            }
            if (tile_screen_y < 0) {
                src_y = -tile_screen_y;
                tile_h += tile_screen_y;
                tile_screen_y = 0;
            }
            if (tile_screen_x + tile_w > fw)
                tile_w = fw - tile_screen_x;
            if (tile_screen_y + tile_h > fh)
                tile_h = fh - tile_screen_y;

            if (tile_w <= 0 || tile_h <= 0)
                return;

            // Blit tile pixels into frame (with alpha blending)
            for (i32 y = 0; y < tile_h; y++) {
                for (i32 x = 0; x < tile_w; x++) {
                    size_t tile_idx =
                        (static_cast<size_t>(src_y + y) * tile.width + static_cast<size_t>(src_x + x)) * 4;
                    size_t frame_idx = (static_cast<size_t>(tile_screen_y + y) * static_cast<size_t>(fw) +
                                        static_cast<size_t>(tile_screen_x + x)) *
                                       4;

                    u8 sa = tile.rgba_pixels[tile_idx + 3];
                    if (sa == 0)
                        continue;

                    if (sa == 255) {
                        frame.rgba_pixels[frame_idx + 0] = tile.rgba_pixels[tile_idx + 0];
                        frame.rgba_pixels[frame_idx + 1] = tile.rgba_pixels[tile_idx + 1];
                        frame.rgba_pixels[frame_idx + 2] = tile.rgba_pixels[tile_idx + 2];
                        frame.rgba_pixels[frame_idx + 3] = 255;
                    } else {
                        f32 a = sa / 255.0f;
                        f32 inv_a = 1.0f - a;
                        frame.rgba_pixels[frame_idx + 0] = static_cast<u8>(tile.rgba_pixels[tile_idx + 0] * a +
                                                                           frame.rgba_pixels[frame_idx + 0] * inv_a);
                        frame.rgba_pixels[frame_idx + 1] = static_cast<u8>(tile.rgba_pixels[tile_idx + 1] * a +
                                                                           frame.rgba_pixels[frame_idx + 1] * inv_a);
                        frame.rgba_pixels[frame_idx + 2] = static_cast<u8>(tile.rgba_pixels[tile_idx + 2] * a +
                                                                           frame.rgba_pixels[frame_idx + 2] * inv_a);
                        frame.rgba_pixels[frame_idx + 3] = 255;
                    }
                }
            }
        }

    }  // namespace

    void Compositor::compose_layer(CompositedFrame &frame, Layer *layer, f32 parent_scroll_x, f32 parent_scroll_y) {
        if (!layer)
            return;

        f32 eff_scroll_x = parent_scroll_x + layer->scroll_offset_x;
        f32 eff_scroll_y = parent_scroll_y + layer->scroll_offset_y;

        // Layer screen position (in viewport coordinates)
        f32 layer_screen_x = layer->bounds.x;
        f32 layer_screen_y = layer->bounds.y;

        // Determine visible tile range
        f32 visible_left = -(layer_screen_x - eff_scroll_x);
        f32 visible_top = -(layer_screen_y - eff_scroll_y);
        f32 visible_right = visible_left + static_cast<f32>(viewport_width_);
        f32 visible_bottom = visible_top + static_cast<f32>(viewport_height_);

        i32 tile_start_x = static_cast<i32>(std::floor(visible_left / TILE_SIZE));
        i32 tile_start_y = static_cast<i32>(std::floor(visible_top / TILE_SIZE));
        i32 tile_end_x = static_cast<i32>(std::ceil(visible_right / TILE_SIZE));
        i32 tile_end_y = static_cast<i32>(std::ceil(visible_bottom / TILE_SIZE));

        for (i32 ty = tile_start_y; ty < tile_end_y; ty++) {
            for (i32 tx = tile_start_x; tx < tile_end_x; tx++) {
                TileKey key{layer->layer_id, tx, ty, 1.0f};
                auto cached = tile_cache_.lookup(key);
                if (cached.has_value()) {
                    blit_tile_to_frame(
                        frame, cached.value(), layer_screen_x, layer_screen_y, eff_scroll_x, eff_scroll_y);
                }
            }
        }

        // Process child layers (drawn on top)
        for (const auto &child : layer->children) {
            compose_layer(frame, child.get(), eff_scroll_x, eff_scroll_y);
        }
    }

}  // namespace browser::render
