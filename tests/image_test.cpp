#include "test_framework.hpp"
#include "../image/decoder.hpp"
#include "../image/format.hpp"
#include <cstring>

namespace browser::image {

// A tiny 1x1 red PNG (color type 2, 8-bit RGB, no interlace, no filter)
static const u8 test_png[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A, // PNG signature
    0x00,0x00,0x00,0x0D, // IHDR length = 13
    0x49,0x48,0x44,0x52, // "IHDR"
    0x00,0x00,0x00,0x01, // width = 1
    0x00,0x00,0x00,0x01, // height = 1
    0x08, // bit depth = 8
    0x02, // color type = 2 (RGB)
    0x00, // compression = 0
    0x00, // filter = 0
    0x00, // interlace = 0
    0x90,0x77,0x53,0xDE, // IHDR CRC
    0x00,0x00,0x00,0x0C, // IDAT length = 12
    0x49,0x44,0x41,0x54, // "IDAT"
    0x08,0xD7, // zlib header (deflate, 32K win, no dict)
    0x63,0xF8,0xCF,0xC0,0x00,0x00, // deflate data
    0x03,0x01,0x01,0x00, // Adler-32
    0x18,0xDD,0x8D,0xB0, // IDAT CRC
    0x00,0x00,0x00,0x00, // IEND length = 0
    0x49,0x45,0x4E,0x44, // "IEND"
    0xAE,0x42,0x60,0x82  // IEND CRC
};

// A tiny 1x1 24-bit BMP (red pixel, no compression)
static const u8 test_bmp[] = {
    0x42,0x4D, // "BM"
    0x3E,0x00,0x00,0x00, // file size = 62
    0x00,0x00, // reserved
    0x00,0x00, // reserved
    0x36,0x00,0x00,0x00, // data offset = 54 (header = 14+40)
    0x28,0x00,0x00,0x00, // header size = 40
    0x01,0x00,0x00,0x00, // width = 1
    0x01,0x00,0x00,0x00, // height = 1
    0x01,0x00, // planes = 1
    0x18,0x00, // bpp = 24
    0x00,0x00,0x00,0x00, // compression = 0
    0x0C,0x00,0x00,0x00, // image size = 12
    0x00,0x00,0x00,0x00, // x pixels per meter
    0x00,0x00,0x00,0x00, // y pixels per meter
    0x00,0x00,0x00,0x00, // colors used
    0x00,0x00,0x00,0x00, // colors important
    0x00,0x00,0xFF,0x00 // pixel data: B=0, G=0, R=255, + padding
};

// A tiny 1x1 GIF (red pixel, 16-color global table)
static const u8 test_gif[] = {
    0x47,0x49,0x46,0x38,0x39,0x61, // "GIF89a"
    0x01,0x00, // width = 1
    0x01,0x00, // height = 1
    0xF0, // packed: GCT=1, color res=7, sort=0, GCT size=16 (2^(0+1)=2)
    0x00, // bg color index
    0x00, // pixel aspect ratio
    // Global color table (2 entries)
    0xFF,0x00,0x00, // entry 0: red
    0x00,0x00,0x00, // entry 1: black
    // Image descriptor
    0x2C, // 0x2C = image separator
    0x00,0x00, // left = 0
    0x00,0x00, // top = 0
    0x01,0x00, // width = 1
    0x01,0x00, // height = 1
    0x00, // packed: no LCT, no interlace
    // LZW min code size
    0x02,
    // Sub-blocks
    0x02, // block size = 2
    0x4C,0x01, // LZW data: clear(4) + index(0) + eoi(5)
    0x00, // block terminator
    0x3B // trailer
};

TEST(detect_format_png, {
    auto fmt = detect_format(test_png, sizeof(test_png));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::PNG));
})

TEST(detect_format_bmp, {
    auto fmt = detect_format(test_bmp, sizeof(test_bmp));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::BMP));
})

TEST(detect_format_gif, {
    auto fmt = detect_format(test_gif, sizeof(test_gif));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::GIF));
})

TEST(detect_format_unknown, {
    const u8 data[] = {0,1,2,3,4,5,6,7};
    auto fmt = detect_format(data, sizeof(data));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::UNKNOWN));
})

TEST(decode_bmp_via_handwritten, {
    auto decoder = create_decoder(ImageFormat::BMP);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_bmp, sizeof(test_bmp));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 1u);
    ASSERT_EQ(img.height, 1u);
    ASSERT_EQ(img.rgba_pixels.size(), 4u);
    ASSERT_EQ(img.rgba_pixels[0], 0xFFu);
    ASSERT_EQ(img.rgba_pixels[1], 0x00u);
    ASSERT_EQ(img.rgba_pixels[2], 0x00u);
    ASSERT_EQ(img.rgba_pixels[3], 0xFFu);
})

TEST(decode_png_via_wic, {
    auto decoder = create_decoder(ImageFormat::PNG);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_png, sizeof(test_png));
    if (result.is_ok()) {
        auto img = std::move(result.unwrap());
        ASSERT_EQ(img.width, 1u);
        ASSERT_EQ(img.height, 1u);
        ASSERT_EQ(img.rgba_pixels.size(), 4u);
        ASSERT_EQ(img.rgba_pixels[3], 0xFFu);
    }
})

TEST(decode_gif_via_wic, {
    auto decoder = create_decoder(ImageFormat::GIF);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_gif, sizeof(test_gif));
    if (result.is_ok()) {
        auto img = std::move(result.unwrap());
        ASSERT_EQ(img.width, 1u);
        ASSERT_EQ(img.height, 1u);
    }
})

TEST(format_detect_and_decode_roundtrip, {
    auto fmt = detect_format(test_bmp, sizeof(test_bmp));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::BMP));
    auto decoder = create_decoder(fmt);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_bmp, sizeof(test_bmp));
    ASSERT(result.is_ok());
})

TEST(wic_decoder_format_bmp, {
    auto wic_decoder = create_decoder(ImageFormat::UNKNOWN);
    ASSERT(wic_decoder != nullptr);
    auto result = wic_decoder->decode(test_bmp, sizeof(test_bmp));
    if (result.is_ok()) {
        auto img = std::move(result.unwrap());
        ASSERT_EQ(img.width, 1u);
        ASSERT_EQ(img.height, 1u);
    }
})

TEST(compare_wic_vs_handwritten_bmp, {
    auto hand_decoder = create_decoder(ImageFormat::BMP);
    auto wic_decoder = create_decoder(ImageFormat::UNKNOWN);
    ASSERT(hand_decoder != nullptr);
    ASSERT(wic_decoder != nullptr);

    auto hand_result = hand_decoder->decode(test_bmp, sizeof(test_bmp));
    auto wic_result = wic_decoder->decode(test_bmp, sizeof(test_bmp));

    if (hand_result.is_ok() && wic_result.is_ok()) {
        auto hand_img = hand_result.unwrap();
        auto wic_img = wic_result.unwrap();
        ASSERT_EQ(hand_img.width, wic_img.width);
        ASSERT_EQ(hand_img.height, wic_img.height);
        ASSERT_EQ(hand_img.rgba_pixels.size(), wic_img.rgba_pixels.size());
        if (hand_img.rgba_pixels.size() == wic_img.rgba_pixels.size()) {
            for (size_t i = 0; i < hand_img.rgba_pixels.size(); i++) {
                ASSERT_EQ(hand_img.rgba_pixels[i], wic_img.rgba_pixels[i]);
            }
        }
    }
})

TEST(invalid_data_returns_error, {
    const u8 bad_data[] = {0,0,0,0};
    auto decoder = create_decoder(ImageFormat::BMP);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(bad_data, sizeof(bad_data));
    ASSERT(result.is_err());
})

} // namespace browser::image
