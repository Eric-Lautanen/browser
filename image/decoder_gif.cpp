#include "decoder.hpp"
#include <cstring>
#include <vector>
#include <algorithm>

namespace browser::image {

#pragma pack(push, 1)
struct GIFHeader {
    char signature[3];
    char version[3];
};

struct GIFLogicalScreen {
    u16 width;
    u16 height;
    u8 packed;
    u8 bg_color_index;
    u8 pixel_aspect_ratio;
};

struct GIFImageDescriptor {
    u16 left;
    u16 top;
    u16 width;
    u16 height;
    u8 packed;
};
#pragma pack(pop)

struct GIFColorTableEntry {
    u8 r, g, b;
};

class GIFDecoder : public Decoder {
public:
    Result<Image> decode(const u8* data, size_t size) override {
        if (size < sizeof(GIFHeader) + sizeof(GIFLogicalScreen)) {
            return Result<Image>("GIF data too small");
        }

        const auto& header = *reinterpret_cast<const GIFHeader*>(data);
        if (std::memcmp(header.signature, "GIF", 3) != 0) {
            return Result<Image>("Invalid GIF signature");
        }

        const auto& ls = *reinterpret_cast<const GIFLogicalScreen*>(data + sizeof(GIFHeader));
        u16 width = ls.width;
        u16 height = ls.height;
        if (width == 0 || height == 0 || width > 4096 || height > 4096) {
            return Result<Image>("Invalid GIF dimensions");
        }

        bool global_color_table = (ls.packed & 0x80) != 0;
        u8 gct_size = 1 << ((ls.packed & 0x07) + 1);

        std::vector<GIFColorTableEntry> global_table;
        size_t pos = sizeof(GIFHeader) + sizeof(GIFLogicalScreen);

        if (global_color_table) {
            for (u8 i = 0; i < gct_size && pos + 3 <= size; i++) {
                GIFColorTableEntry entry;
                entry.r = data[pos++];
                entry.g = data[pos++];
                entry.b = data[pos++];
                global_table.push_back(entry);
            }
        }

        // Initialize output with background color
        std::vector<u8> rgba(width * height * 4, 0);
        u8 bg_index = ls.bg_color_index;
        if (bg_index < global_table.size()) {
            for (u32 i = 0; i < width * height; i++) {
                rgba[i * 4 + 0] = global_table[bg_index].r;
                rgba[i * 4 + 1] = global_table[bg_index].g;
                rgba[i * 4 + 2] = global_table[bg_index].b;
                rgba[i * 4 + 3] = 255;
            }
        }

        // Skip extension blocks, find image descriptor
        bool image_found = false;
        while (pos < size) {
            u8 block_type = data[pos++];

            if (block_type == 0x21) {
                // Extension block - skip extension label byte
                if (pos >= size) break;
                pos++; // skip ext_label
                // Skip sub-blocks
                while (pos < size) {
                    u8 block_size = data[pos++];
                    if (block_size == 0) break;
                    pos += block_size;
                    if (pos > size) break;
                }
            } else if (block_type == 0x2C) {
                // Image Descriptor
                if (pos + sizeof(GIFImageDescriptor) > size) break;
                const auto& id = *reinterpret_cast<const GIFImageDescriptor*>(data + pos);
                pos += sizeof(GIFImageDescriptor);

                bool local_color_table = (id.packed & 0x80) != 0;
                bool interlace = (id.packed & 0x40) != 0;
                u8 lct_size = 1 << ((id.packed & 0x07) + 1);

                std::vector<GIFColorTableEntry> color_table;
                if (local_color_table) {
                    for (u8 i = 0; i < lct_size && pos + 3 <= size; i++) {
                        GIFColorTableEntry entry;
                        entry.r = data[pos++];
                        entry.g = data[pos++];
                        entry.b = data[pos++];
                        color_table.push_back(entry);
                    }
                } else {
                    color_table = global_table;
                }

                // LZW minimum code size
                if (pos >= size) break;
                u8 min_code_size = data[pos++];

                // Skip sub-blocks of LZW data, but collect all bytes
                std::vector<u8> lzw_data;
                while (pos < size) {
                    u8 block_size = data[pos++];
                    if (block_size == 0) break;
                    lzw_data.insert(lzw_data.end(), data + pos, data + pos + block_size);
                    pos += block_size;
                    if (pos > size) break;
                }

                // LZW Decompress
                std::vector<u8> indexed = lzw_decompress(lzw_data, min_code_size,
                                                          static_cast<u32>(id.width) * static_cast<u32>(id.height));

                if (color_table.empty()) {
                    return Result<Image>("No color table available for GIF");
                }

                // Deinterlace if needed
                std::vector<u8> indexed_deinterlaced = indexed;
                u32 img_w = id.width;
                u32 img_h = id.height;

                if (interlace && img_w > 0 && img_h > 0) {
                    indexed_deinterlaced.resize(img_w * img_h);
                    // GIF interlace: pass 1 (every 8 rows starting at 0), pass 2 (every 8 starting at 4),
                    // pass 3 (every 4 starting at 2), pass 4 (every 2 starting at 1)
                    u32 pass_row = 0;
                    const u32 start_row[] = {0, 4, 2, 1};
                    const u32 increment[] = {8, 8, 4, 2};
                    for (int pass = 0; pass < 4; pass++) {
                        for (u32 r = start_row[pass]; r < img_h; r += increment[pass]) {
                            for (u32 c = 0; c < img_w && pass_row * img_w + c < indexed.size(); c++) {
                                indexed_deinterlaced[r * img_w + c] = indexed[pass_row * img_w + c];
                            }
                            pass_row++;
                        }
                    }
                }

                // Copy to RGBA output
                for (u32 gy = 0; gy < img_h && id.top + gy < height; gy++) {
                    for (u32 gx = 0; gx < img_w && id.left + gx < width; gx++) {
                        u8 index = indexed_deinterlaced[gy * img_w + gx];
                        u32 dst_off = ((id.top + gy) * width + (id.left + gx)) * 4;
                        if (index < color_table.size()) {
                            rgba[dst_off + 0] = color_table[index].r;
                            rgba[dst_off + 1] = color_table[index].g;
                            rgba[dst_off + 2] = color_table[index].b;
                            rgba[dst_off + 3] = 255;
                        }
                    }
                }

                image_found = true;
            } else if (block_type == 0x3B) {
                // Trailer
                break;
            } else {
                // Unknown block type - skip
                break;
            }
        }

        if (!image_found) {
            return Result<Image>("No image data found in GIF");
        }

        Image img;
        img.width = width;
        img.height = height;
        img.format = ImageFormat::GIF;
        img.rgba_pixels = std::move(rgba);
        return Result<Image>(std::move(img));
    }

private:
    std::vector<u8> lzw_decompress(const std::vector<u8>& data, u8 min_code_size, u32 max_pixels) {
        if (data.empty()) return {};

        u32 clear_code = 1u << min_code_size;
        u32 eoi_code = clear_code + 1;
        u32 code_size = min_code_size + 1;
        u32 next_code = eoi_code + 1;
        u32 max_code = (1u << code_size);

        struct TableEntry {
            std::vector<u8> data;
        };
        std::vector<TableEntry> table;
        for (u32 i = 0; i < clear_code; i++) {
            table.push_back({ {static_cast<u8>(i)} });
        }
        table.push_back({}); // clear code placeholder
        table.push_back({}); // eoi code placeholder

        std::vector<u8> output;
        u32 bit_pos = 0;
        u32 old_code = 0;
        bool first = true;

        auto read_bits = [&](u32 bits) -> u32 {
            u32 val = 0;
            for (u32 i = 0; i < bits; i++) {
                u32 byte_pos = bit_pos / 8;
                u32 bit_offset = bit_pos % 8;
                if (byte_pos >= data.size()) return val;
                if (data[byte_pos] & (1u << bit_offset)) {
                    val |= (1u << i);
                }
                bit_pos++;
            }
            return val;
        };

        while (output.size() < max_pixels) {
            u32 code = read_bits(code_size);

            if (code == eoi_code) break;

            if (code == clear_code) {
                code_size = min_code_size + 1;
                max_code = (1u << code_size);
                next_code = eoi_code + 1;
                table.resize(next_code);
                first = true;
                continue;
            }

            if (first) {
                if (code < table.size()) {
                    output.push_back(table[code].data[0]);
                }
                old_code = code;
                first = false;
                continue;
            }

            std::vector<u8> entry;
            if (code < table.size()) {
                entry = table[code].data;
            } else if (code == table.size()) {
                entry = table[old_code].data;
                entry.push_back(entry[0]);
            } else {
                break;
            }

            for (u8 b : entry) {
                if (output.size() >= max_pixels) break;
                output.push_back(b);
            }

            // Add new entry to table
            if (next_code <= 4095) {
                std::vector<u8> new_entry = table[old_code].data;
                new_entry.push_back(entry[0]);
                if (next_code >= table.size()) {
                    table.push_back({std::move(new_entry)});
                }
                next_code++;

                if (next_code > max_code && code_size < 12) {
                    code_size++;
                    max_code = (1u << code_size);
                }
            }

            old_code = code;
        }

        return output;
    }
};

std::unique_ptr<Decoder> create_gif_decoder() {
    return std::make_unique<GIFDecoder>();
}

} // namespace browser::image
