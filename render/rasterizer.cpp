#include "rasterizer.hpp"

#include "texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace browser::render {

    namespace {

        bool rects_intersect(f32 ax, f32 ay, f32 aw, f32 ah, f32 bx, f32 by, f32 bw, f32 bh) {
            return ax < bx + bw && ax + aw > bx && ay < bh + by && ay + ah > by;
        }

        f32 clampf(f32 v, f32 lo, f32 hi) {
            return std::max(lo, std::min(hi, v));
        }

    }  // namespace

    bool Rasterizer::PixelBuffer::clip_rect(i32 &rx, i32 &ry, i32 &rw, i32 &rh) const {
        i32 w = static_cast<i32>(width);
        i32 h = static_cast<i32>(height);
        if (rx < 0) {
            rw += rx;
            rx = 0;
        }
        if (ry < 0) {
            rh += ry;
            ry = 0;
        }
        if (rx + rw > w)
            rw = w - rx;
        if (ry + rh > h)
            rh = h - ry;
        return rw > 0 && rh > 0;
    }

    void Rasterizer::PixelBuffer::fill_rect(i32 rx, i32 ry, i32 rw, i32 rh, u8 r, u8 g, u8 b, u8 a) {
        if (!clip_rect(rx, ry, rw, rh))
            return;

        for (i32 y = ry; y < ry + rh; y++) {
            for (i32 x = rx; x < rx + rw; x++) {
                size_t idx = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
                if (a == 255) {
                    pixels[idx + 0] = r;
                    pixels[idx + 1] = g;
                    pixels[idx + 2] = b;
                    pixels[idx + 3] = a;
                } else if (a > 0) {
                    f32 src_a = a / 255.0f;
                    f32 dst_a = pixels[idx + 3] / 255.0f;
                    f32 out_a = src_a + dst_a * (1.0f - src_a);
                    if (out_a > 0) {
                        pixels[idx + 0] = static_cast<u8>(
                            (r / 255.0f * src_a + (pixels[idx + 0] / 255.0f) * dst_a * (1.0f - src_a)) / out_a * 255);
                        pixels[idx + 1] = static_cast<u8>(
                            (g / 255.0f * src_a + (pixels[idx + 1] / 255.0f) * dst_a * (1.0f - src_a)) / out_a * 255);
                        pixels[idx + 2] = static_cast<u8>(
                            (b / 255.0f * src_a + (pixels[idx + 2] / 255.0f) * dst_a * (1.0f - src_a)) / out_a * 255);
                        pixels[idx + 3] = static_cast<u8>(out_a * 255);
                    }
                }
            }
        }
    }

    RasterizedTile Rasterizer::rasterize_tile(
        const Layer &layer, i32 tile_x, i32 tile_y, f32 layer_scroll_x, f32 layer_scroll_y) {
        RasterizedTile tile;
        tile.layer_id = layer.layer_id;
        tile.tile_x = tile_x;
        tile.tile_y = tile_y;
        tile.width = TILE_SIZE;
        tile.height = TILE_SIZE;
        tile.rgba_pixels.resize(TILE_SIZE * TILE_SIZE * 4, 0);

        if (!layer.display_list)
            return tile;

        PixelBuffer buf;
        buf.width = TILE_SIZE;
        buf.height = TILE_SIZE;
        buf.pixels.swap(tile.rgba_pixels);

        // Tile coverage in layer-local space
        f32 tile_min_x = static_cast<f32>(tile_x * TILE_SIZE) - layer_scroll_x;
        f32 tile_min_y = static_cast<f32>(tile_y * TILE_SIZE) - layer_scroll_y;

        for (const auto &cmd : layer.display_list->commands()) {
            execute_command(buf, cmd, tile_min_x, tile_min_y, layer_scroll_x, layer_scroll_y);
        }

        tile.rgba_pixels.swap(buf.pixels);
        return tile;
    }

    void Rasterizer::execute_command(PixelBuffer &buf,
                                     const PaintCommand &cmd,
                                     f32 tile_origin_x,
                                     f32 tile_origin_y,
                                     f32 layer_scroll_x,
                                     f32 layer_scroll_y) {
        (void)layer_scroll_x;
        (void)layer_scroll_y;

        switch (cmd.type) {
            case PaintCommand::Type::FILL_RECT:
            case PaintCommand::Type::DRAW_SHADOW: {
                f32 cx = cmd.rect.x;
                f32 cy = cmd.rect.y;
                f32 cw = cmd.rect.width;
                f32 ch = cmd.rect.height;

                // Transform to tile-local
                f32 lx = cx - tile_origin_x;
                f32 ly = cy - tile_origin_y;

                // Check intersection with tile
                if (!rects_intersect(lx, ly, cw, ch, 0, 0, static_cast<f32>(TILE_SIZE), static_cast<f32>(TILE_SIZE))) {
                    return;
                }

                Color c = cmd.color;
                buf.fill_rect(static_cast<i32>(lx),
                              static_cast<i32>(ly),
                              static_cast<i32>(cw),
                              static_cast<i32>(ch),
                              static_cast<u8>(clampf(c.r, 0, 1) * 255),
                              static_cast<u8>(clampf(c.g, 0, 1) * 255),
                              static_cast<u8>(clampf(c.b, 0, 1) * 255),
                              static_cast<u8>(clampf(c.a, 0, 1) * 255));
                break;
            }
            case PaintCommand::Type::DRAW_ROUNDED_RECT: {
                f32 cx = cmd.rect.x;
                f32 cy = cmd.rect.y;
                f32 cw = cmd.rect.width;
                f32 ch = cmd.rect.height;

                f32 lx = cx - tile_origin_x;
                f32 ly = cy - tile_origin_y;

                if (!rects_intersect(lx, ly, cw, ch, 0, 0, static_cast<f32>(TILE_SIZE), static_cast<f32>(TILE_SIZE))) {
                    return;
                }

                Color c = cmd.color;
                buf.fill_rect(static_cast<i32>(lx),
                              static_cast<i32>(ly),
                              static_cast<i32>(cw),
                              static_cast<i32>(ch),
                              static_cast<u8>(clampf(c.r, 0, 1) * 255),
                              static_cast<u8>(clampf(c.g, 0, 1) * 255),
                              static_cast<u8>(clampf(c.b, 0, 1) * 255),
                              static_cast<u8>(clampf(c.a, 0, 1) * 255));
                break;
            }
            case PaintCommand::Type::DRAW_TEXT: {
                f32 cx = cmd.rect.x;
                f32 cy = cmd.rect.y;
                f32 cw = cmd.rect.width;
                f32 ch = cmd.rect.height;
                f32 lx = cx - tile_origin_x;
                f32 ly = cy - tile_origin_y;
                if (!rects_intersect(lx, ly, cw, ch, 0, 0, static_cast<f32>(TILE_SIZE), static_cast<f32>(TILE_SIZE)))
                    break;
                Color c = cmd.color;
                buf.fill_rect(static_cast<i32>(lx),
                              static_cast<i32>(ly),
                              static_cast<i32>(cw),
                              static_cast<i32>(ch),
                              static_cast<u8>(clampf(c.r, 0, 1) * 255),
                              static_cast<u8>(clampf(c.g, 0, 1) * 255),
                              static_cast<u8>(clampf(c.b, 0, 1) * 255),
                              static_cast<u8>(clampf(c.a, 0, 1) * 255));
                break;
            }
            case PaintCommand::Type::DRAW_IMAGE: {
                f32 cx = cmd.rect.x;
                f32 cy = cmd.rect.y;
                f32 cw = cmd.rect.width;
                f32 ch = cmd.rect.height;
                f32 lx = cx - tile_origin_x;
                f32 ly = cy - tile_origin_y;
                if (!rects_intersect(lx, ly, cw, ch, 0, 0, static_cast<f32>(TILE_SIZE), static_cast<f32>(TILE_SIZE)))
                    break;
                Color c = cmd.color;
                buf.fill_rect(static_cast<i32>(lx),
                              static_cast<i32>(ly),
                              static_cast<i32>(cw),
                              static_cast<i32>(ch),
                              static_cast<u8>(clampf(c.r, 0, 1) * 255),
                              static_cast<u8>(clampf(c.g, 0, 1) * 255),
                              static_cast<u8>(clampf(c.b, 0, 1) * 255),
                              static_cast<u8>(clampf(c.a, 0, 1) * 255));
                break;
            }
            case PaintCommand::Type::DRAW_GRADIENT: {
                f32 cx = cmd.rect.x;
                f32 cy = cmd.rect.y;
                f32 cw = cmd.rect.width;
                f32 ch = cmd.rect.height;
                f32 lx = cx - tile_origin_x;
                f32 ly = cy - tile_origin_y;
                if (!rects_intersect(lx, ly, cw, ch, 0, 0, static_cast<f32>(TILE_SIZE), static_cast<f32>(TILE_SIZE)))
                    break;
                buf.fill_rect(static_cast<i32>(lx),
                              static_cast<i32>(ly),
                              static_cast<i32>(cw),
                              static_cast<i32>(ch),
                              255,
                              255,
                              255,
                              255);
                break;
            }
            default:
                break;
        }
    }

    std::unique_ptr<Texture2D> Rasterizer::upload_tile(const RasterizedTile &tile) {
        auto tex = std::make_unique<Texture2D>();
        auto r = tex->create(tile.width, tile.height, tile.rgba_pixels.data(), true);
        if (r.is_err())
            return nullptr;
        return tex;
    }

}  // namespace browser::render
