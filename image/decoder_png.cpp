#include "../net/deflate.hpp"
#include "decoder.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace browser::image {

    static u32 read_u32_be(const u8 *p) {
        return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) | (static_cast<u32>(p[2]) << 8) |
               static_cast<u32>(p[3]);
    }

    // PaethPredictor for filter reconstruction
    static u8 paeth_predictor(u8 a, u8 b, u8 c) {
        i32 p = static_cast<i32>(a) + static_cast<i32>(b) - static_cast<i32>(c);
        i32 pa = std::abs(p - static_cast<i32>(a));
        i32 pb = std::abs(p - static_cast<i32>(b));
        i32 pc = std::abs(p - static_cast<i32>(c));
        if (pa <= pb && pa <= pc)
            return a;
        if (pb <= pc)
            return b;
        return c;
    }

    enum class ColorType { GRAYSCALE = 0, RGB = 2, INDEXED = 3, GRAYSCALE_ALPHA = 4, RGBA = 6 };

    class PNGDecoder : public Decoder {
    public:
        Result<Image> decode(const u8 *data, size_t size) override {
            if (size < 8)
                return Result<Image>("PNG data too small");

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

                if (pos + 12 + chunk_len > size)
                    break;

                const u8 *chunk_data = data + pos + 8;

                if (std::strcmp(chunk_type, "IHDR") == 0) {
                    if (chunk_len < 13)
                        return Result<Image>("Invalid IHDR");
                    width = read_u32_be(chunk_data);
                    height = read_u32_be(chunk_data + 4);
                    bit_depth = chunk_data[8];
                    color_type = chunk_data[9];
                    u8 compression = chunk_data[10];
                    u8 filter = chunk_data[11];

                    if (compression != 0)
                        return Result<Image>("Unsupported PNG compression");
                    if (filter != 0)
                        return Result<Image>("Unsupported PNG filter");
                    u8 interlace = chunk_data[12];
                    if (interlace != 0)
                        return Result<Image>("Interlaced PNG not supported");
                    // I-C5: reject (color_type, bit_depth) pairs outside the spec table.
                    bool depth_ok = false;
                    switch (color_type) {
                        case 0:
                            depth_ok =
                                bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16;
                            break;
                        case 2:
                        case 4:
                        case 6:
                            depth_ok = bit_depth == 8 || bit_depth == 16;
                            break;
                        case 3:
                            depth_ok = bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8;
                            break;
                        default:
                            return Result<Image>("Unsupported PNG color type");
                    }
                    if (!depth_ok)
                        return Result<Image>("Invalid PNG color type / bit depth combination");
                    has_ihdr = true;
                } else if (std::strcmp(chunk_type, "PLTE") == 0) {
                    if (chunk_len > 0 && chunk_len % 3 == 0) {
                        palette.assign(chunk_data, chunk_data + chunk_len);
                    }
                } else if (std::strcmp(chunk_type, "tRNS") == 0) {
                    palette_alpha.assign(chunk_data, chunk_data + chunk_len);
                } else if (std::strcmp(chunk_type, "IDAT") == 0) {
                    compressed_data.insert(compressed_data.end(), chunk_data, chunk_data + chunk_len);
                } else if (std::strcmp(chunk_type, "IEND") == 0) {
                    break;
                }

                pos += 12 + chunk_len;
            }

            if (!has_ihdr)
                return Result<Image>("No IHDR chunk");
            if (width == 0 || height == 0 || width > 4096 || height > 4096) {
                return Result<Image>("Invalid PNG dimensions");
            }

            // Determine bytes per pixel
            u32 channels = 1;
            switch (color_type) {
                case 0:
                    channels = 1;
                    break;
                case 2:
                    channels = 3;
                    break;
                case 3:
                    channels = 1;
                    break;
                case 4:
                    channels = 2;
                    break;
                case 6:
                    channels = 4;
                    break;
                default:
                    return Result<Image>("Unsupported PNG color type");
            }

            // Decompress using Deflate. IDAT already carries its own zlib wrapper
            // (RFC 1950); inflate() detects and validates it — no fabricated header.
            std::vector<u8> decompressed;
            if (!compressed_data.empty()) {
                decompressed = net::inflate(compressed_data.data(), static_cast<u32>(compressed_data.size()));
            }

            if (decompressed.empty()) {
                return Result<Image>("Failed to decompress PNG data");
            }

            // 16-bit depths: scale down to 8 bits by keeping the high byte of each
            // big-endian sample, so the rest of the decoder sees byte-per-sample data.
            if (bit_depth == 16) {
                std::vector<u8> narrow;
                narrow.reserve(decompressed.size() / 2);
                for (u32 y = 0; y < height; y++) {
                    u32 row_start = static_cast<u32>(y) * (width * channels * 2 + 1);
                    if (row_start >= decompressed.size())
                        break;
                    narrow.push_back(decompressed[row_start]);  // filter byte
                    for (u32 b = 0; b < width * channels; b++) {
                        size_t idx = row_start + 1 + b * 2;
                        if (idx >= decompressed.size())
                            break;
                        narrow.push_back(decompressed[idx]);
                    }
                }
                decompressed = std::move(narrow);
                bit_depth = 8;
            }

            u32 bpp = channels;  // byte depths only reach here (16-bit narrowed above)
            if (bit_depth < 8)
                bpp = 1;
            u32 scanline_width = width;
            if (bit_depth < 8) {
                scanline_width = (width * bit_depth + 7) / 8;
            } else {
                scanline_width = width * bpp;
            }

            // I-perf: reconstruct scanlines IN PLACE over the inflate output.
            // The old path kept two extra full-frame intermediates (raw_image
            // with per-row filter bytes, plus a reconstructed buffer) before
            // the RGBA conversion; each row is now unfiltered where it lies,
            // reading already-reconstructed neighbours from the same buffer.
            for (u32 y = 0; y < height; y++) {
                u32 row_offset = y * (scanline_width + 1);
                if (row_offset >= decompressed.size())
                    break;
                u8 filter_type = decompressed[row_offset];
                if (filter_type > 4)
                    return Result<Image>("Invalid PNG filter type");

                for (u32 x = 0; x < scanline_width; x++) {
                    size_t idx = row_offset + 1 + x;
                    if (idx >= decompressed.size())
                        break;
                    u8 raw_x = decompressed[idx];
                    u8 a = 0, b = 0, c = 0;

                    if (x >= bpp)
                        a = decompressed[row_offset + 1 + x - bpp];
                    if (y > 0)
                        b = decompressed[(y - 1) * (scanline_width + 1) + 1 + x];
                    if (x >= bpp && y > 0)
                        c = decompressed[(y - 1) * (scanline_width + 1) + 1 + x - bpp];

                    switch (filter_type) {
                        case 0:
                            break;
                        case 1:
                            raw_x += a;
                            break;
                        case 2:
                            raw_x += b;
                            break;
                        case 3:
                            raw_x += static_cast<u8>((static_cast<u16>(a) + static_cast<u16>(b)) / 2);
                            break;
                        case 4:
                            raw_x += paeth_predictor(a, b, c);
                            break;
                        default:
                            break;
                    }

                    decompressed[idx] = raw_x;
                }
            }

            // Strided accessor: row y's reconstructed bytes start after its
            // filter byte.
            auto recon_at = [&](u32 y, u32 off) -> u8 {
                size_t idx = static_cast<size_t>(y) * (scanline_width + 1) + 1 + off;
                return idx < decompressed.size() ? decompressed[idx] : 0;
            };

            // Convert to RGBA
            std::vector<u8> rgba(width * height * 4, 0);

            for (u32 y = 0; y < height; y++) {
                for (u32 x = 0; x < width; x++) {
                    u32 pixel_offset = (y * width + x) * 4;

                    if (color_type == 3) {
                        // Indexed
                        u8 index = 0;
                        if (bit_depth == 8) {
                            index = recon_at(y, x);
                        } else if (bit_depth == 4) {
                            u8 byte_val = recon_at(y, x / 2);
                            index = (x % 2 == 0) ? ((byte_val >> 4) & 0x0F) : (byte_val & 0x0F);
                        } else if (bit_depth == 2) {
                            u8 byte_val = recon_at(y, x / 4);
                            index = (byte_val >> (6 - (x % 4) * 2)) & 0x03;
                        } else if (bit_depth == 1) {
                            u8 byte_val = recon_at(y, x / 8);
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
                        u8 gray = recon_at(y, x);
                        rgba[pixel_offset + 0] = gray;
                        rgba[pixel_offset + 1] = gray;
                        rgba[pixel_offset + 2] = gray;
                        rgba[pixel_offset + 3] = 255;
                    } else if (color_type == 4) {
                        // Grayscale + alpha
                        u8 gray = recon_at(y, x * 2);
                        u8 alpha = recon_at(y, x * 2 + 1);
                        rgba[pixel_offset + 0] = gray;
                        rgba[pixel_offset + 1] = gray;
                        rgba[pixel_offset + 2] = gray;
                        rgba[pixel_offset + 3] = alpha;
                    } else if (color_type == 2) {
                        // RGB
                        rgba[pixel_offset + 0] = recon_at(y, x * 3 + 0);
                        rgba[pixel_offset + 1] = recon_at(y, x * 3 + 1);
                        rgba[pixel_offset + 2] = recon_at(y, x * 3 + 2);
                        rgba[pixel_offset + 3] = 255;
                    } else if (color_type == 6) {
                        // RGBA
                        rgba[pixel_offset + 0] = recon_at(y, x * 4 + 0);
                        rgba[pixel_offset + 1] = recon_at(y, x * 4 + 1);
                        rgba[pixel_offset + 2] = recon_at(y, x * 4 + 2);
                        rgba[pixel_offset + 3] = recon_at(y, x * 4 + 3);
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

}  // namespace browser::image
