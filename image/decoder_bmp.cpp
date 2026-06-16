#include "decoder.hpp"
#include <cstring>
#include <algorithm>

namespace browser::image {

#pragma pack(push, 1)
struct BMPFileHeader {
    u16 type;
    u32 size;
    u16 reserved1;
    u16 reserved2;
    u32 data_offset;
};

struct BMPInfoHeader {
    u32 header_size;
    i32 width;
    i32 height;
    u16 planes;
    u16 bpp;
    u32 compression;
    u32 image_size;
    i32 x_pixels_per_meter;
    i32 y_pixels_per_meter;
    u32 colors_used;
    u32 colors_important;
};
#pragma pack(pop)

class BMPDecoder : public Decoder {
public:
    Result<Image> decode(const u8* data, size_t size) override {
        if (size < sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)) {
            return Result<Image>("BMP data too small for headers");
        }

        const auto& fh = *reinterpret_cast<const BMPFileHeader*>(data);
        if (fh.type != 0x4D42) { // 'BM'
            return Result<Image>("Not a valid BMP file");
        }

        const auto& ih = *reinterpret_cast<const BMPInfoHeader*>(data + sizeof(BMPFileHeader));
        if (ih.header_size < 40) {
            return Result<Image>("Unsupported BMP header version");
        }

        u32 w = static_cast<u32>(std::abs(ih.width));
        u32 h = static_cast<u32>(std::abs(ih.height));
        bool top_down = ih.height < 0;

        if (w == 0 || h == 0 || w > 4096 || h > 4096) {
            return Result<Image>("Invalid BMP dimensions");
        }

        u32 data_offset = fh.data_offset;
        if (data_offset > size) {
            return Result<Image>("BMP data offset out of range");
        }

        u32 palette_offset = sizeof(BMPFileHeader) + ih.header_size;
        u32 num_palette_colors = 0;
        if (ih.bpp <= 8) {
            num_palette_colors = (ih.colors_used > 0) ? ih.colors_used : (1u << ih.bpp);
            if (num_palette_colors > 256) num_palette_colors = 256;
        }

        std::vector<u8> rgba(w * h * 4, 0);

        auto get_palette_color = [&](u8 index) -> u32 {
            if (index >= num_palette_colors) {
                return 0xFF000000;
            }
            u32 entry_offset = palette_offset + static_cast<u32>(index) * 4;
            if (entry_offset + 4 > size) return 0xFF000000;
            const u8* p = data + entry_offset;
            return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
                   (static_cast<u32>(p[2]) << 16) | (0xFFu << 24);
        };

        u32 row_bytes = ((w * ih.bpp + 31) / 32) * 4;

        // Handle RLE8 (compression=1), RLE4 (compression=2), or BITFIELDS (compression=3)
        if (ih.compression == 1) {
            std::vector<u8> decoded(w * h, 0);
            u32 dx = 0, dy = 0;
            u32 src_pos = data_offset;
            while (dy < h && src_pos + 1 < size) {
                u8 byte1 = data[src_pos++];
                u8 byte2 = data[src_pos++];
                if (byte1 == 0) {
                    if (byte2 == 0) { dy++; dx = 0; }
                    else if (byte2 == 1) { break; }
                    else if (byte2 == 2) {
                        if (src_pos + 1 >= size) break;
                        dx += data[src_pos++];
                        dy += data[src_pos++];
                    } else {
                        for (u32 i = 0; i < byte2 && src_pos < size; i++) {
                            if (dx < w && dy < h) decoded[dy * w + dx] = data[src_pos];
                            dx++; src_pos++;
                        }
                        if (byte2 % 2 != 0) src_pos++;
                    }
                } else {
                    for (u32 i = 0; i < byte1 && dx < w; i++) {
                        if (dx < w && dy < h) decoded[dy * w + dx] = byte2;
                        dx++;
                    }
                }
            }
            for (u32 y = 0; y < h; y++) {
                for (u32 x = 0; x < w; x++) {
                    u8 index = decoded[y * w + x];
                    u32 col = get_palette_color(index);
                    u32 po = (y * w + x) * 4;
                    rgba[po + 0] = (col) & 0xFF;
                    rgba[po + 1] = (col >> 8) & 0xFF;
                    rgba[po + 2] = (col >> 16) & 0xFF;
                    rgba[po + 3] = (col >> 24) & 0xFF;
                }
            }
        } else if (ih.compression == 2) {
            return Result<Image>("BMP RLE4 compression not supported");
        } else if (ih.compression != 0 && ih.compression != 3) {
            return Result<Image>("Unsupported BMP compression: " + std::to_string(ih.compression));
        } else {
            // Uncompressed or BITFIELDS: decode raw pixel data
            for (u32 y = 0; y < h; y++) {
                u32 src_row = top_down ? y : (h - 1 - y);
                const u8* row_data = data + data_offset + src_row * row_bytes;
                u32 dst_y = y;

                for (u32 x = 0; x < w; x++) {
                    u32 pixel_offset = (dst_y * w + x) * 4;
                    u8 r = 0, g = 0, b = 0, a = 255;

                    switch (ih.bpp) {
                        case 32: {
                            u32 pixel_offset_bmp = x * 4;
                            if (pixel_offset_bmp + 3 < row_bytes) {
                                b = row_data[pixel_offset_bmp + 0];
                                g = row_data[pixel_offset_bmp + 1];
                                r = row_data[pixel_offset_bmp + 2];
                                a = row_data[pixel_offset_bmp + 3];
                            }
                            break;
                        }
                        case 24: {
                            u32 pixel_offset_bmp = x * 3;
                            if (pixel_offset_bmp + 2 < row_bytes) {
                                b = row_data[pixel_offset_bmp + 0];
                                g = row_data[pixel_offset_bmp + 1];
                                r = row_data[pixel_offset_bmp + 2];
                            }
                            break;
                        }
                        case 16: {
                            u32 pixel_offset_bmp = x * 2;
                            if (pixel_offset_bmp + 1 < row_bytes) {
                                u16 pixel = static_cast<u16>(row_data[pixel_offset_bmp]) |
                                            (static_cast<u16>(row_data[pixel_offset_bmp + 1]) << 8);
                                r = static_cast<u8>(((pixel >> 10) & 0x1F) * 255 / 31);
                                g = static_cast<u8>(((pixel >> 5) & 0x1F) * 255 / 31);
                                b = static_cast<u8>((pixel & 0x1F) * 255 / 31);
                            }
                            break;
                        }
                        case 8: {
                            if (x < row_bytes) {
                                u8 index = row_data[x];
                                u32 col = get_palette_color(index);
                                r = (col >> 16) & 0xFF;
                                g = (col >> 8) & 0xFF;
                                b = col & 0xFF;
                                a = (col >> 24) & 0xFF;
                            }
                            break;
                        }
                        case 4: {
                            u8 index = row_data[x / 2];
                            if (x % 2 == 0) {
                                index = (index >> 4) & 0x0F;
                            } else {
                                index = index & 0x0F;
                            }
                            u32 col = get_palette_color(index);
                            r = (col >> 16) & 0xFF;
                            g = (col >> 8) & 0xFF;
                            b = col & 0xFF;
                            a = (col >> 24) & 0xFF;
                            break;
                        }
                        case 1: {
                            u8 byte_val = row_data[x / 8];
                            u8 index = static_cast<u8>((byte_val >> (7 - (x % 8))) & 1);
                            u32 col = get_palette_color(index);
                            r = (col >> 16) & 0xFF;
                            g = (col >> 8) & 0xFF;
                            b = col & 0xFF;
                            a = (col >> 24) & 0xFF;
                            break;
                        }
                    }

                    rgba[pixel_offset + 0] = r;
                    rgba[pixel_offset + 1] = g;
                    rgba[pixel_offset + 2] = b;
                    rgba[pixel_offset + 3] = a;
                }
            }
        }

        Image img;
        img.width = w;
        img.height = h;
        img.format = ImageFormat::BMP;
        img.rgba_pixels = std::move(rgba);
        return Result<Image>(std::move(img));
    }
};

std::unique_ptr<Decoder> create_bmp_decoder() {
    return std::make_unique<BMPDecoder>();
}

} // namespace browser::image
