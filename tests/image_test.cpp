#include "../image/decoder.hpp"
#include "../image/format.hpp"
#include "test_framework.hpp"

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

TEST(decode_png, {
    auto decoder = create_decoder(ImageFormat::PNG);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_png, sizeof(test_png));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 1u);
    ASSERT_EQ(img.height, 1u);
    ASSERT_EQ(img.rgba_pixels.size(), 4u);
    ASSERT_EQ(img.rgba_pixels[3], 0xFFu);
})

TEST(decode_gif, {
    auto decoder = create_decoder(ImageFormat::GIF);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_gif, sizeof(test_gif));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 1u);
    ASSERT_EQ(img.height, 1u);
})

TEST(format_detect_and_decode_roundtrip, {
    auto fmt = detect_format(test_bmp, sizeof(test_bmp));
    ASSERT_EQ(static_cast<int>(fmt), static_cast<int>(ImageFormat::BMP));
    auto decoder = create_decoder(fmt);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_bmp, sizeof(test_bmp));
    ASSERT(result.is_ok());
})

// A minimal 8x8 grayscale JPEG (all pixels = 128 = medium gray)
// Uses simplified Huffman tables: 1 code for DC (category 0) and 1 code for AC (EOB)
static const u8 test_jpeg[] = {
    0xFF, 0xD8, // SOI
    // APP0 (JFIF)
    0xFF, 0xE0, 0x00, 0x10, // marker + length
    0x4A, 0x46, 0x49, 0x46, 0x00, // "JFIF\0"
    0x01, 0x01, // version 1.1
    0x00, // units
    0x00, 0x01, // X density
    0x00, 0x01, // Y density
    0x00, 0x00, // no thumbnail
    // DQT (luminance table 0, all 1s)
    0xFF, 0xDB, 0x00, 0x43, 0x00, // marker, length=67, precision=0, table=0
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    // SOF0 (baseline, 8x8, 1 component)
    0xFF, 0xC0, 0x00, 0x0B, // marker + length=11
    0x08, // precision=8
    0x00, 0x08, // height=8
    0x00, 0x08, // width=8
    0x01, // 1 component
    0x01, 0x11, 0x00, // ID=1, sampling=0x11, QTable=0
    // DHT (DC table 0): 1 code, category 0 = "0"
    0xFF, 0xC4, 0x00, 0x14, // marker + length=20
    0x00, // class=0(DC), table=0
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // counts: 1 code of length 1
    0x00, // value: category 0
    // DHT (AC table 0): 1 code, EOB = "0"
    0xFF, 0xC4, 0x00, 0x14, // marker + length=20
    0x10, // class=1(AC), table=0
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // counts: 1 code of length 1
    0x00, // value: EOB
    // SOS (start of scan)
    0xFF, 0xDA, 0x00, 0x08, // marker + length=8
    0x01, // 1 component
    0x01, 0x00, // ID=1, DC table=0, AC table=0
    0x00, 0x3F, 0x00, // SS=0, SE=63, Ah=0, Al=0
    // Entropy-coded data: DC=cat0(1bit "0") + AC=EOB(1bit "0") = 2 bits, padded
    0x3F,
    // EOI
    0xFF, 0xD9
};

TEST(decode_jpeg, {
    auto decoder = create_decoder(ImageFormat::JPEG);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(test_jpeg, sizeof(test_jpeg));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 8u);
    ASSERT_EQ(img.height, 8u);
    ASSERT_EQ(img.rgba_pixels.size(), 8u * 8u * 4u);
    // All pixels should be medium gray (128)
    for (u32 i = 0; i < 8 * 8 * 4; i += 4) {
        ASSERT_EQ(img.rgba_pixels[i + 0], 128u);
        ASSERT_EQ(img.rgba_pixels[i + 1], 128u);
        ASSERT_EQ(img.rgba_pixels[i + 2], 128u);
        ASSERT_EQ(img.rgba_pixels[i + 3], 255u);
    }
})

TEST(invalid_data_returns_error, {
    const u8 bad_data[] = {0,0,0,0};
    auto decoder = create_decoder(ImageFormat::BMP);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(bad_data, sizeof(bad_data));
    ASSERT(result.is_err());
})

TEST(jpeg_truncated_dht_rejected, {
    // DHT segment claims a full 16-byte counts array but the file ends mid-way.
    std::vector<u8> d = {0xFF, 0xD8};
    d.insert(d.end(), {0xFF, 0xC4, 0x00, 0x14, 0x00});
    d.push_back(0x01);  // only 1 of 16 count bytes present
    auto decoder = create_decoder(ImageFormat::JPEG);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

// SOS with table-selector nibble >= 4 (I-C4): needs a parseable SOF first, then a
// truncated-but-valid SOS header carrying Td/Ta = 15. The decoder must reject the
// selectors rather than indexing huff_[15][...] out of bounds.
TEST(jpeg_sos_bad_table_selector_rejected, {
    std::vector<u8> d;
    auto push = [&d](std::initializer_list<u8> bytes) { d.insert(d.end(), bytes); };
    push({0xFF, 0xD8});
    // DQT: table 0 all 1s
    push({0xFF, 0xDB, 0x00, 0x43, 0x00});
    for (int i = 0; i < 64; i++) d.push_back(0x01);
    // SOF0: 8x8, 1 component
    push({0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x08, 0x00, 0x08, 0x01, 0x01, 0x11, 0x00});
    // DHT DC table 0 and AC table 0
    push({0xFF, 0xC4, 0x00, 0x14, 0x00});
    for (int i = 0; i < 16; i++) d.push_back(0x00);
    d.push_back(0x01);
    push({0x01});  // value byte
    push({0xFF, 0xC4, 0x00, 0x14, 0x10});
    for (int i = 0; i < 16; i++) d.push_back(0x00);
    d.push_back(0x01);
    push({0x01});
    // SOS with Td=15, Ta=15
    push({0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0xFF, 0x00, 0x3F, 0x00});

    auto decoder = create_decoder(ImageFormat::JPEG);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

// A real 16x16 4:2:0 baseline JPEG produced by GDI+ (quality 50).
// Y sampling 2x2, chroma 1x1 â€” the exact geometry that triggered I-C1.
static const u8 jpeg_420_16x16[] = {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00,
    0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x10, 0x0B, 0x0C, 0x0E, 0x0C, 0x0A, 0x10, 0x0E, 0x0D, 0x0E, 0x12, 0x11, 0x10,
    0x13, 0x18, 0x28, 0x1A, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25, 0x1D, 0x28, 0x3A, 0x33, 0x3D, 0x3C, 0x39, 0x33,
    0x38, 0x37, 0x40, 0x48, 0x5C, 0x4E, 0x40, 0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6D, 0x51, 0x57, 0x5F, 0x62, 0x67,
    0x68, 0x67, 0x3E, 0x4D, 0x71, 0x79, 0x70, 0x64, 0x78, 0x5C, 0x65, 0x67, 0x63, 0xFF, 0xDB, 0x00, 0x43, 0x01, 0x11,
    0x12, 0x12, 0x18, 0x15, 0x18, 0x2F, 0x1A, 0x1A, 0x2F, 0x63, 0x42, 0x38, 0x42, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
    0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x10, 0x00, 0x10, 0x03, 0x01, 0x22, 0x00,
    0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00,
    0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62,
    0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A,
    0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00,
    0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04,
    0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31,
    0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09,
    0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A,
    0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8,
    0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9,
    0xFA, 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00, 0xE7, 0xED, 0xEC, 0x7A,
    0x71, 0x5A, 0x76, 0xF6, 0x3D, 0x38, 0xAD, 0x3B, 0x7B, 0x1E, 0x9C, 0x56, 0x9D, 0xBD, 0x8F, 0x4E, 0x2A, 0xAA, 0xE2,
    0xC3, 0x2F, 0xC7, 0x6D, 0xA9, 0xFF, 0xD9,
};

TEST(jpeg_baseline_420_decodes, {
    auto decoder = create_decoder(ImageFormat::JPEG);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(jpeg_420_16x16, sizeof(jpeg_420_16x16));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 16u);
    ASSERT_EQ(img.height, 16u);
    ASSERT_EQ(img.rgba_pixels.size(), 16u * 16u * 4u);
    // Alpha fully opaque; pixel values deterministic for this fixture
    ASSERT_EQ(img.rgba_pixels[3], 255u);
    ASSERT_EQ(img.rgba_pixels[(15u * 16u + 15u) * 4u + 3], 255u);
})

// Hostile JPEGs (I-C4): untrusted table-index fields must be rejected at parse
// time instead of indexing q_tables_[4][64] / huff_[4][2] out of bounds.

static std::vector<u8> hostile_jpeg_with_sof(u8 sof_marker, const std::vector<u8> &sof_body) {
    std::vector<u8> d = {0xFF, 0xD8};       // SOI
    d.insert(d.end(), {0xFF, sof_marker});  // SOF
    u16 len = static_cast<u16>(sof_body.size() + 2);
    d.push_back(static_cast<u8>(len >> 8));
    d.push_back(static_cast<u8>(len & 0xFF));
    d.insert(d.end(), sof_body.begin(), sof_body.end());
    return d;
}

TEST(jpeg_sof_bad_quant_table_rejected, {
    // SOF0 body: prec=8, h=8, w=8, nc=1, comp{ id=1, samp=0x11, Tq=7 }
    std::vector<u8> body = {0x08, 0x00, 0x08, 0x00, 0x08, 0x01, 0x01, 0x11, 0x07};
    auto d = hostile_jpeg_with_sof(0xC0, body);
    auto decoder = create_decoder(ImageFormat::JPEG);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

TEST(jpeg_sof_zero_sampling_rejected, {
    // h_samp = v_samp = 0 would cause division by zero downstream.
    std::vector<u8> body = {0x08, 0x00, 0x08, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00};
    auto d = hostile_jpeg_with_sof(0xC0, body);
    auto decoder = create_decoder(ImageFormat::JPEG);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

TEST(jpeg_progressive_reports_unsupported, {
    // SOF2 body identical layout to baseline.
    std::vector<u8> body = {0x08, 0x00, 0x08, 0x00, 0x08, 0x01, 0x01, 0x11, 0x00};
    auto d = hostile_jpeg_with_sof(0xC2, body);
    auto decoder = create_decoder(ImageFormat::JPEG);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

// ---- PNG (I-C2 / I-C5) ----

// Hand-built 1x1 8-bit grayscale PNG with pixel value 0xAB.
// zlib stream: 78 01 | stored block: 01 02 00 FD FF | 00 AB | Adler(00 AB) = 00 AD 00 AC
static const u8 png_gray_1x1[] = {0x89,
                                  0x50,
                                  0x4E,
                                  0x47,
                                  0x0D,
                                  0x0A,
                                  0x1A,
                                  0x0A,  // signature
                                  // IHDR: len=13, w=1, h=1, depth=8, color=0 (gray), comp=0, filter=0, interlace=0
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x0D,
                                  'I',
                                  'H',
                                  'D',
                                  'R',
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x01,
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x01,
                                  0x08,
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x00,
                                  0xC6,
                                  0x78,
                                  0x27,
                                  0x4F,  // IHDR CRC (precomputed)
                                  // IDAT with real zlib wrapper + valid Adler-32
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x0D,
                                  'I',
                                  'D',
                                  'A',
                                  'T',
                                  0x78,
                                  0x01,
                                  0x01,
                                  0x02,
                                  0x00,
                                  0xFD,
                                  0xFF,
                                  0x00,
                                  0xAB,
                                  0x00,
                                  0xAD,
                                  0x00,
                                  0xAC,
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x00,  // IDAT CRC — unchecked by our decoder
                                  // IEND
                                  0x00,
                                  0x00,
                                  0x00,
                                  0x00,
                                  'I',
                                  'E',
                                  'N',
                                  'D',
                                  0xAE,
                                  0x42,
                                  0x60,
                                  0x82};

static std::vector<u8> png_with_ihdr(u8 bit_depth, u8 color_type, u8 interlace) {
    std::vector<u8> d = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    auto be32 = [&d](u32 v) {
        d.push_back(static_cast<u8>(v >> 24));
        d.push_back(static_cast<u8>((v >> 16) & 0xFF));
        d.push_back(static_cast<u8>((v >> 8) & 0xFF));
        d.push_back(static_cast<u8>(v & 0xFF));
    };
    be32(13);
    d.insert(d.end(), {'I', 'H', 'D', 'R'});
    be32(2);
    be32(2);  // 2x2
    d.push_back(bit_depth);
    d.push_back(color_type);
    d.push_back(0);  // compression
    d.push_back(0);  // filter
    d.push_back(interlace);
    be32(0);  // CRC placeholder — not verified
    return d;
}

TEST(png_real_zlib_stream_decodes_exactly, {
    auto decoder = create_decoder(ImageFormat::PNG);
    ASSERT(decoder != nullptr);
    auto result = decoder->decode(png_gray_1x1, sizeof(png_gray_1x1));
    ASSERT(result.is_ok());
    auto img = std::move(result.unwrap());
    ASSERT_EQ(img.width, 1u);
    ASSERT_EQ(img.height, 1u);
    ASSERT_EQ(img.rgba_pixels.size(), 4u);
    ASSERT_EQ(img.rgba_pixels[0], 0xABu);
    ASSERT_EQ(img.rgba_pixels[1], 0xABu);
    ASSERT_EQ(img.rgba_pixels[2], 0xABu);
    ASSERT_EQ(img.rgba_pixels[3], 255u);
})

// I-C5: illegal (color_type, bit_depth) combos previously produced OOB reads.
TEST(png_illegal_combo_rgb_depth4_rejected, {
    auto data = png_with_ihdr(4, 2, 0);
    auto decoder = create_decoder(ImageFormat::PNG);
    auto result = decoder->decode(data.data(), data.size());
    ASSERT(result.is_err());
})

TEST(png_illegal_combo_palette_depth16_rejected, {
    auto data = png_with_ihdr(16, 3, 0);
    auto decoder = create_decoder(ImageFormat::PNG);
    auto result = decoder->decode(data.data(), data.size());
    ASSERT(result.is_err());
})

TEST(png_interlaced_rejected_explicitly, {
    auto data = png_with_ihdr(8, 6, 1);
    auto decoder = create_decoder(ImageFormat::PNG);
    auto result = decoder->decode(data.data(), data.size());
    ASSERT(result.is_err());
})

// ---- Hostile BMP / GIF (I-C3 / I-H2 / I-H3) ----

TEST(bmp_oversized_dimensions_truncated_rejected, {
    // 62-byte file whose header claims 4096x4096 @24bpp — the exact I-C3 repro.
    std::vector<u8> d(test_bmp, test_bmp + sizeof(test_bmp));
    auto set32 = [&](size_t off, u32 v) {
        d[off] = static_cast<u8>(v);
        d[off + 1] = static_cast<u8>(v >> 8);
        d[off + 2] = static_cast<u8>(v >> 16);
        d[off + 3] = static_cast<u8>(v >> 24);
    };
    set32(18, 4096);   // info-header width
    set32(22, 4096);   // info-header height
    auto decoder = create_decoder(ImageFormat::BMP);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

TEST(bmp_data_offset_past_pixels_rejected, {
    // Valid 1x1 but data offset points beyond EOF.
    std::vector<u8> d(test_bmp, test_bmp + sizeof(test_bmp));
    auto set32 = [&](size_t off, u32 v) {
        d[off] = static_cast<u8>(v);
        d[off + 1] = static_cast<u8>(v >> 8);
        d[off + 2] = static_cast<u8>(v >> 16);
        d[off + 3] = static_cast<u8>(v >> 24);
    };
    set32(10, 5000);   // fh.data_offset
    auto decoder = create_decoder(ImageFormat::BMP);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

TEST(bmp_unsupported_bit_depth_rejected, {
    std::vector<u8> d(test_bmp, test_bmp + sizeof(test_bmp));
    // info header starts at 14; bpp is a u16 at +14 within it
    d[14 + 14] = 2;
    d[14 + 15] = 0;
    auto decoder = create_decoder(ImageFormat::BMP);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

// GIF with GCT size field = 7 (256 entries) but no table bytes present:
// pre-I-H2 the u8 wrap produced an empty palette and parser desync instead
// of a clean error.
static std::vector<u8> gif_with_packed(u8 packed) {
    std::vector<u8> d;
    auto push16 = [&d](u16 v) {
        d.push_back(static_cast<u8>(v & 0xFF));
        d.push_back(static_cast<u8>(v >> 8));
    };
    d.insert(d.end(), {'G', 'I', 'F', '8', '9', 'a'});
    push16(4);   // logical width
    push16(4);   // logical height
    d.push_back(packed);
    d.push_back(0);  // bg index
    d.push_back(0);  // aspect
    return d;
}

TEST(gif_gct_size_field_seven_no_truncation_desync, {
    // packed = 0xF7: GCT present, size field 7 → 256 entries that don't exist.
    auto d = gif_with_packed(0xF7);
    auto decoder = create_decoder(ImageFormat::GIF);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());  // truncated global color table
})

TEST(gif_truncated_lzw_subblock_rejected, {
    // Valid 2-entry GCT then an image descriptor whose LZW sub-block claims
    // more bytes than remain (I-H3 read-past-buffer).
    auto d = gif_with_packed(0x80 | 1);  // GCT present, 4 entries... use F1-style: size=1 → 4 entries
    // provide only 2 of 4 entries? Keep it simple: full 4-entry table = 12 bytes
    for (int i = 0; i < 12; i++) d.push_back(0x10 * (i % 15));
    // Image descriptor
    d.push_back(0x2C);
    d.push_back(0); d.push_back(0);   // left
    d.push_back(0); d.push_back(0);   // top
    d.push_back(4); d.push_back(0);   // w
    d.push_back(4); d.push_back(0);   // h
    d.push_back(0x00);                // packed: no LCT, no interlace
    d.push_back(0x02);                // LZW min code size
    d.push_back(10);                  // sub-block claims 10 bytes...
    d.push_back(0xAB); d.push_back(0xCD);  // ...only 2 present, then EOF

    auto decoder = create_decoder(ImageFormat::GIF);
    auto result = decoder->decode(d.data(), d.size());
    ASSERT(result.is_err());
})

} // namespace browser::image
