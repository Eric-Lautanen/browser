#include "test_framework.hpp"
#include "../render/canvas.hpp"

using namespace browser;
using namespace browser::render;

// R-S6: dimension product overflow — 65535 * 32768 * 4 wrapped u32 pre-fix and
// the undersized buffer was then written past via set_pixel (heap corruption).
TEST(canvas_area_overflow_rejected, {
    auto canvas = std::make_shared<Canvas2D>(65535, 32768);
    // Area cap clamps the allocation to a sane size instead of wrapping.
    ASSERT((u64)canvas->width() * canvas->height() <= 16ull * 1024 * 1024);
    // Filling the full (clamped) area must not corrupt memory.
    canvas->fill_rect(0, 0, (f32)canvas->width(), (f32)canvas->height());
    ASSERT(canvas->width() > 0);
    ASSERT(canvas->height() > 0);
})

TEST(canvas_resize_area_overflow_rejected, {
    Canvas2D canvas(100, 100);
    canvas.resize(65535, 65535);
    ASSERT((u64)canvas.width() * canvas.height() <= 16ull * 1024 * 1024);
})

TEST(canvas_normal_dims_preserved, {
    Canvas2D canvas(300, 150);
    ASSERT_EQ(canvas.width(), 300u);
    ASSERT_EQ(canvas.height(), 150u);
})

// R-S7: getImageData(0,0,65536,65536) previously resized out to sw*sh*4 == 0
// and wrote through the resulting zero-length vector.
TEST(canvas_get_image_data_overflow_safe, {
    Canvas2D canvas(8, 8);
    canvas.fill_rect(0, 0, 8, 8);
    std::vector<u8> out;
    canvas.get_image_data(0, 0, 65536, 65536, out);
    // Clamped to the canvas rect, never a wrapped zero.
    ASSERT_EQ(out.size(), (size_t)8 * 8 * 4);
})

TEST(canvas_get_image_data_clamps_and_reads_pixels, {
    Canvas2D canvas(4, 4);
    canvas.set_fill_style(1.0f, 0.0f, 0.0f, 1.0f);
    canvas.fill_rect(0, 0, 4, 4);
    std::vector<u8> out;
    canvas.get_image_data(2, 2, 10, 10, out);  // request exceeds bounds → clamp
    ASSERT_EQ(out.size(), (size_t)2 * 2 * 4);
    ASSERT_EQ(out[0], 255u);   // red
    ASSERT_EQ(out[1], 0u);
    ASSERT_EQ(out[2], 0u);
    ASSERT_EQ(out[3], 255u);   // opaque
})
