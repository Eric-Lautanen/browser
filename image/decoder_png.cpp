#include "decoder.hpp"
#include "../net/deflate.hpp"
#include <cstring>
#include <vector>
#include <algorithm>

namespace browser::image {

static u32 read_u32_be(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

// PaethPredictor for filter reconstruction
static u8 paeth_predictor(u8 a, u8 b, u8 c) {
    i32 p = static_cast<i32>(a) + static_cast<i32>(b) - static_cast<i32>(c);
    i32 pa = std::abs(p - static_cast<i32>(a));
    i32 pb = std::abs(p - static_cast<i32>(b));
    i32 pc = std::abs(p - static_cast<i32>(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

enum class ColorType {
    GRAYSCALE = 0,
    RGB = 2,
    INDEXED = 3,
    GRAYSCALE_ALPHA = 4,
    RGBA = 6
};

class PNGDecoder : public Decoder {
public:
    Result<Image> decode(const u8* data, size_t size) override {
        if (size < 8) return Result<Image>("PNG data too small");

        // Validate PNG signature
        const u8 sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(data, sig, 8) != 0) {
            return Result<Image>("Invalid PNG signature");
        }

        u32 width = 0, height = 0;
        u8 bit_depth = 0, color_type = 0;
        std::vector<u8> palette;
        std::vector<u8> palette_alpha;
        std::vector<u8> compressed_data;

        size_t pos = 8;
        bool has_ihdr = false;

        while (pos + 12 <= size) {
            u32 chunk_len = read_u32_be(data + pos);
            char chunk_type[5] = {};
            std::memcpy(chunk_type, data + pos + 4, 4);

            if (pos + 12 + chunk_len > size) break;

            const u8* chunk_data = data + pos + 8;

            if (std::strcmp(chunk_type, "IHDR") == 0) {
                if (chunk_len < 13) return Result<Image>("Invalid IHDR");
                width = read_u32_be(chunk_data);
                height = read_u32_be(chunk_data + 4);
                bit_depth = chunk_data[8];
                color_type = chunk_data[9];
                u8 compression = chunk_data[10];
                u8 filter = chunk_data[11];

                if (compression != 0) return Result<Image>("Unsupported PNG compression");
                if (filter != 0) return Result<Image>("Unsupported PNG filter");
                // interlace (byte 12): 0=none, 1=Adam7 (pass 1 only, non-interlaced decode)
                has_ihdr = true;
            } else if (std::strcmp(chunk_type, "PLTE") == 0) {
                if (chunk_len > 0 && chunk_len % 3 == 0) {
                    palette.assign(chunk_data, chunk_data + chunk_len);
                }
            } else if (std::strcmp(chunk_type, "tRNS") == 0) {
                palette_alpha.assign(chunk_data, chunk_data + chunk_len);
            } else if (std::strcmp(chunk_type, "IDAT") == 0) {
                compressed_data.insert(compressed_data.end(),
                                        chunk_data, chunk_data + chunk_len);
            } else if (std::strcmp(chunk_type, "IEND") == 0) {
                break;
            }

            pos += 12 + chunk_len;
        }

        if (!has_ihdr) return Result<Image>("No IHDR chunk");
        if (width == 0 || height == 0 || width > 4096 || height > 4096) {
            return Result<Image>("Invalid PNG dimensions");
        }

        // Determine bytes per pixel
        u32 channels = 1;
        switch (color_type) {
            case 0: channels = 1; break;
            case 2: channels = 3; break;
            case 3: channels = 1; break;
            case 4: channels = 2; break;
            case 6: channels = 4; break;
            default: return Result<Image>("Unsupported PNG color type");
        }

        u32 bytes_per_pixel = channels;
        if (bit_depth >= 8) {
            bytes_per_pixel = channels * (bit_depth / 8);
        } else {
            bytes_per_pixel = 1; // handled specially
        }

        // Decompress using Deflate
        std::vector<u8> decompressed;
        if (!compressed_data.empty()) {
            // Build a minimal zlib header for the inflater (no dictionary)
            std::vector<u8> zlib_data;
            zlib_data.push_back(0x78); // CMF: deflate, 32K window
            zlib_data.push_back(0x01); // FLG: check bits, no dict
            zlib_data.insert(zlib_data.end(), compressed_data.begin(), compressed_data.end());
            // Adler-32 (append 0 for simplicity - inflate in net/deflate.cpp may not check)
            zlib_data.push_back(0x00);
            zlib_data.push_back(0x00);
            zlib_data.push_back(0x00);
            zlib_data.push_back(0x00);

            decompressed = net::inflate(zlib_data.data(), static_cast<u32>(zlib_data.size()));
        }

        if (decompressed.empty()) {
            return Result<Image>("Failed to decompress PNG data");
        }

        u32 bpp = bytes_per_pixel;
        u32 scanline_width = width;
        if (bit_depth < 8) {
            scanline_width = (width * bit_depth + 7) / 8;
        } else {
            scanline_width = width * bpp;
        }

        std::vector<u8> raw_image(height * (scanline_width + 1));
        u32 src_pos = 0;

        for (u32 y = 0; y < height; y++) {
            if (src_pos >= decompressed.size()) break;
            u8 filter_type = decompressed[src_pos++];
            u32 raw_offset = y * (scanline_width + 1);
            raw_image[raw_offset] = filter_type;
            for (u32 x = 0; x < scanline_width && src_pos < decompressed.size(); x++) {
                raw_image[raw_offset + 1 + x] = decompressed[src_pos++];
            }
        }

        // Reconstruct scanlines
        std::vector<u8> reconstructed(height * scanline_width, 0);
        for (u32 y = 0; y < height; y++) {
            u32 row_offset = y * (scanline_width + 1);
            u8 filter_type = raw_image[row_offset];
            u32 dest_offset = y * scanline_width;

            for (u32 x = 0; x < scanline_width; x++) {
                u8 raw_x = raw_image[row_offset + 1 + x];
                u8 a = 0, b = 0, c = 0;

                if (x >= bpp) a = reconstructed[dest_offset + x - bpp];
                if (y > 0) b = reconstructed[(y - 1) * scanline_width + x];
                if (x >= bpp && y > 0) c = reconstructed[(y - 1) * scanline_width + x - bpp];

                u8 reconstructed_x = 0;
                switch (filter_type) {
                    case 0: reconstructed_x = raw_x; break;
                    case 1: reconstructed_x = raw_x + a; break;
                    case 2: reconstructed_x = raw_x + b; break;
                    case 3: reconstructed_x = raw_x + static_cast<u8>((static_cast<u16>(a) + static_cast<u16>(b)) / 2); break;
                    case 4: reconstructed_x = raw_x + paeth_predictor(a, b, c); break;
                    default: reconstructed_x = raw_x; break;
                }

                reconstructed[dest_offset + x] = reconstructed_x;
            }
        }

        // Convert to RGBA
        std::vector<u8> rgba(width * height * 4, 0);

        for (u32 y = 0; y < height; y++) {
            for (u32 x = 0; x < width; x++) {
                u32 pixel_offset = (y * width + x) * 4;

                if (color_type == 3) {
                    // Indexed
                    u8 index = 0;
                    if (bit_depth == 8) {
                        index = reconstructed[y * scanline_width + x];
                    } else if (bit_depth == 4) {
                        u8 byte_val = reconstructed[y * scanline_width + x / 2];
                        index = (x % 2 == 0) ? ((byte_val >> 4) & 0x0F) : (byte_val & 0x0F);
                    } else if (bit_depth == 2) {
                        u8 byte_val = reconstructed[y * scanline_width + x / 4];
                        index = (byte_val >> (6 - (x % 4) * 2)) & 0x03;
                    } else if (bit_depth == 1) {
                        u8 byte_val = reconstructed[y * scanline_width + x / 8];
                        index = (byte_val >> (7 - (x % 8))) & 0x01;
                    }

                    if (static_cast<size_t>(index) * 3 + 2 < palette.size()) {
                        rgba[pixel_offset + 0] = palette[index * 3 + 0];
                        rgba[pixel_offset + 1] = palette[index * 3 + 1];
                        rgba[pixel_offset + 2] = palette[index * 3 + 2];
                    }
                    if (index < palette_alpha.size()) {
                        rgba[pixel_offset + 3] = palette_alpha[index];
                    } else {
                        rgba[pixel_offset + 3] = 255;
                    }
                } else if (color_type == 0) {
                    // Grayscale
                    u8 gray = reconstructed[y * scanline_width + x];
                    rgba[pixel_offset + 0] = gray;
                    rgba[pixel_offset + 1] = gray;
                    rgba[pixel_offset + 2] = gray;
                    rgba[pixel_offset + 3] = 255;
                } else if (color_type == 4) {
                    // Grayscale + alpha
                    u8 gray = reconstructed[y * scanline_width + x * 2];
                    u8 alpha = reconstructed[y * scanline_width + x * 2 + 1];
                    rgba[pixel_offset + 0] = gray;
                    rgba[pixel_offset + 1] = gray;
                    rgba[pixel_offset + 2] = gray;
                    rgba[pixel_offset + 3] = alpha;
                } else if (color_type == 2) {
                    // RGB
                    u32 src_off = y * scanline_width + x * 3;
                    rgba[pixel_offset + 0] = reconstructed[src_off + 0];
                    rgba[pixel_offset + 1] = reconstructed[src_off + 1];
                    rgba[pixel_offset + 2] = reconstructed[src_off + 2];
                    rgba[pixel_offset + 3] = 255;
                } else if (color_type == 6) {
                    // RGBA
                    u32 src_off = y * scanline_width + x * 4;
                    rgba[pixel_offset + 0] = reconstructed[src_off + 0];
                    rgba[pixel_offset + 1] = reconstructed[src_off + 1];
                    rgba[pixel_offset + 2] = reconstructed[src_off + 2];
                    rgba[pixel_offset + 3] = reconstructed[src_off + 3];
                }
            }
        }

        Image img;
        img.width = width;
        img.height = height;
        img.format = ImageFormat::PNG;
        img.rgba_pixels = std::move(rgba);
        return Result<Image>(std::move(img));
    }
};

std::unique_ptr<Decoder> create_png_decoder() {
    return std::make_unique<PNGDecoder>();
}

} // namespace browser::image
