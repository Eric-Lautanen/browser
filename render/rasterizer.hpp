#pragma once
#include "../platform/opengl.hpp"
#include "layer_tree.hpp"

#include <memory>
#include <vector>

namespace browser::render {

    struct RasterizedTile {
        u32 layer_id;
        i32 tile_x;
        i32 tile_y;
        u32 width;
        u32 height;
        std::vector<u8> rgba_pixels;
    };

    class Rasterizer {
    public:
        static constexpr u32 TILE_SIZE = 256;

        // Rasterize a tile for a given layer at the given tile coordinates.
        // The tile covers (tile_x * TILE_SIZE, tile_y * TILE_SIZE) to
        // ((tile_x+1) * TILE_SIZE, (tile_y+1) * TILE_SIZE) in layer space.
        static RasterizedTile rasterize_tile(
            const Layer &layer, i32 tile_x, i32 tile_y, f32 layer_scroll_x, f32 layer_scroll_y);

        // Upload a rasterized tile to an OpenGL texture.
        // Returns a new Texture2D if successful, nullptr otherwise.
        static std::unique_ptr<class Texture2D> upload_tile(const RasterizedTile &tile);

    private:
        struct PixelBuffer {
            u32 width;
            u32 height;
            std::vector<u8> pixels;

            void fill_rect(i32 rx, i32 ry, i32 rw, i32 rh, u8 r, u8 g, u8 b, u8 a);
            bool clip_rect(i32 &rx, i32 &ry, i32 &rw, i32 &rh) const;
        };

        static void execute_command(PixelBuffer &buf,
                                    const PaintCommand &cmd,
                                    f32 tile_origin_x,
                                    f32 tile_origin_y,
                                    f32 layer_scroll_x,
                                    f32 layer_scroll_y);
    };

}  // namespace browser::render
