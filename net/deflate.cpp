#include "deflate.hpp"
#include "../async/task.hpp"
#include "../async/executor.hpp"
#include <cstring>

namespace browser::net {

struct BitReader {
    const u8* data;
    u32 len;
    u32 byte_pos = 0;
    u32 bit_pos = 0;

    BitReader(const u8* d, u32 l) : data(d), len(l) {}

    u32 read_bit() {
        if (byte_pos >= len) return 0;
        u32 b = (data[byte_pos] >> bit_pos) & 1;
        bit_pos++;
        if (bit_pos >= 8) { bit_pos = 0; byte_pos++; }
        return b;
    }

    u32 read_bits(u32 count) {
        u32 val = 0;
        for (u32 i = 0; i < count; i++) {
            val |= read_bit() << i;
        }
        return val;
    }

    void align_to_byte() {
        if (bit_pos > 0) { bit_pos = 0; byte_pos++; }
    }

    u32 read_byte() {
        if (byte_pos >= len) return 0;
        u32 b = data[byte_pos++];
        return b;
    }

    u32 read_u16_le() {
        u32 lo = read_byte();
        u32 hi = read_byte();
        return lo | (hi << 8);
    }

    bool eof() const { return byte_pos >= len && bit_pos == 0; }
};

struct HuffmanTree {
    struct Node {
        u16 children[2] = {0, 0};
        u16 symbol = 0;
        bool is_leaf = false;
    };
    std::vector<Node> nodes;

    HuffmanTree() { nodes.push_back({}); }

    void build(const u16* code_lens, u32 num_symbols) {
        u32 max_len = 0;
        for (u32 i = 0; i < num_symbols; i++) {
            if (code_lens[i] > max_len) max_len = code_lens[i];
        }
        if (max_len == 0) return;

        u16 bl_count[16] = {};
        for (u32 i = 0; i < num_symbols; i++) {
            if (code_lens[i] > 0 && code_lens[i] < 16) {
                bl_count[code_lens[i]]++;
            }
        }

        u16 next_code[16] = {};
        u16 code = 0;
        for (u32 bits = 1; bits <= max_len; bits++) {
            code = (code + bl_count[bits - 1]) << 1;
            next_code[bits] = code;
        }

        nodes.clear();
        nodes.push_back({});

        for (u32 sym = 0; sym < num_symbols; sym++) {
            u32 len = code_lens[sym];
            if (len == 0 || len >= 16) continue;

            u16 code_val = next_code[len];
            next_code[len]++;

            u32 idx = 0;
            for (i32 b = static_cast<i32>(len) - 1; b >= 0; b--) {
                u32 bit = (code_val >> static_cast<u32>(b)) & 1;
                if (nodes[idx].children[bit] == 0) {
                    nodes[idx].children[bit] = static_cast<u16>(nodes.size());
                    nodes.push_back({});
                }
                idx = nodes[idx].children[bit];
            }
            nodes[idx].is_leaf = true;
            nodes[idx].symbol = static_cast<u16>(sym);
        }
    }

    u32 decode(BitReader& br) const {
        u32 idx = 0;
        while (!nodes[idx].is_leaf) {
            u32 bit = br.read_bit();
            u32 next = nodes[idx].children[bit];
            if (next >= nodes.size() || next == 0) return 0xFFFF;
            idx = next;
        }
        return nodes[idx].symbol;
    }
};

// Fixed Huffman code lengths for literals/lengths (256-279: 7 bits, 280-287: 8 bits, 0-143: 8 bits, 144-255: 9 bits)
static void build_fixed_literal_tree(HuffmanTree& tree) {
    u16 lens[288] = {};
    for (u32 i = 0; i <= 143; i++) lens[i] = 8;
    for (u32 i = 144; i <= 255; i++) lens[i] = 9;
    for (u32 i = 256; i <= 279; i++) lens[i] = 7;
    for (u32 i = 280; i <= 287; i++) lens[i] = 8;
    tree.build(lens, 288);
}

static void build_fixed_distance_tree(HuffmanTree& tree) {
    u16 lens[32] = {};
    for (u32 i = 0; i < 32; i++) lens[i] = 5;
    tree.build(lens, 32);
}

static const u32 kLenBase[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const u32 kLenExtra[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const u32 kDistBase[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};

static const u32 kDistExtra[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static u32 decode_length(u32 sym, BitReader& br) {
    if (sym < 257) return sym;
    u32 idx = sym - 257;
    if (idx >= 29) return 0;
    return kLenBase[idx] + br.read_bits(kLenExtra[idx]);
}

static u32 decode_distance(u32 sym, BitReader& br) {
    if (sym >= 30) return 0;
    return kDistBase[sym] + br.read_bits(kDistExtra[sym]);
}

static std::vector<u8> inflate_internal(const u8* data, u32 len) {
    BitReader br(data, len);
    std::vector<u8> result;
    result.reserve(len * 4); // estimate

    HuffmanTree fixed_literal, fixed_distance;
    build_fixed_literal_tree(fixed_literal);
    build_fixed_distance_tree(fixed_distance);

    bool bfinal = false;
    while (!bfinal && !br.eof()) {
        bfinal = br.read_bit() != 0;
        u32 btype = br.read_bits(2);

        if (btype == 0) {
            // Stored block
            br.align_to_byte();
            u32 stored_len = br.read_u16_le();
            /* u16 nlen = */ br.read_u16_le(); // one's complement
            for (u32 i = 0; i < stored_len && br.byte_pos < br.len; i++) {
                result.push_back(static_cast<u8>(br.read_byte()));
            }
        } else if (btype == 1 || btype == 2) {
            const HuffmanTree* lit_tree;
            const HuffmanTree* dist_tree;

            HuffmanTree dynamic_literal, dynamic_distance;
            if (btype == 2) {
                // Dynamic Huffman
                u32 hlit = br.read_bits(5) + 257;
                u32 hdist = br.read_bits(5) + 1;
                u32 hclen = br.read_bits(4) + 4;

                static const u32 kCodeLenOrder[] = {
                    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
                };

                u16 code_len_lens[19] = {};
                for (u32 i = 0; i < hclen; i++) {
                    code_len_lens[kCodeLenOrder[i]] = static_cast<u16>(br.read_bits(3));
                }

                HuffmanTree code_len_tree;
                code_len_tree.build(code_len_lens, 19);

                u16 code_values[288 + 32] = {};
                u32 total_symbols = hlit + hdist;
                u32 cv_idx = 0;
                while (cv_idx < total_symbols) {
                    u32 sym = code_len_tree.decode(br);
                    if (sym == 0xFFFF) break;
                    if (sym < 16) {
                        code_values[cv_idx++] = static_cast<u16>(sym);
                    } else if (sym == 16) {
                        u32 repeat = br.read_bits(2) + 3;
                        u16 prev = cv_idx > 0 ? code_values[cv_idx - 1] : 0;
                        for (u32 j = 0; j < repeat && cv_idx < total_symbols; j++) {
                            code_values[cv_idx++] = prev;
                        }
                    } else if (sym == 17) {
                        u32 repeat = br.read_bits(3) + 3;
                        for (u32 j = 0; j < repeat && cv_idx < total_symbols; j++) {
                            code_values[cv_idx++] = 0;
                        }
                    } else if (sym == 18) {
                        u32 repeat = br.read_bits(7) + 11;
                        for (u32 j = 0; j < repeat && cv_idx < total_symbols; j++) {
                            code_values[cv_idx++] = 0;
                        }
                    }
                }

                dynamic_literal.build(code_values, hlit);
                dynamic_distance.build(code_values + hlit, hdist);
                lit_tree = &dynamic_literal;
                dist_tree = &dynamic_distance;
            } else {
                lit_tree = &fixed_literal;
                dist_tree = &fixed_distance;
            }

            // Decompress block
            while (true) {
                u32 sym = lit_tree->decode(br);
                if (sym == 0xFFFF) break;
                if (sym < 256) {
                    result.push_back(static_cast<u8>(sym));
                } else if (sym == 256) {
                    break; // end of block
                } else {
                    u32 length = decode_length(sym, br);
                    u32 dist_sym = dist_tree->decode(br);
                    if (dist_sym == 0xFFFF) break;
                    u32 distance = decode_distance(dist_sym, br);
                    if (distance == 0 || distance > result.size()) {
                        distance = 1;
                    }
                    u32 start = static_cast<u32>(result.size()) - distance;
                    for (u32 i = 0; i < length; i++) {
                        result.push_back(result[start + i]);
                    }
                }
            }
        }
    }

    return result;
}

std::vector<u8> inflate(const u8* data, u32 len) {
    return inflate_internal(data, len);
}

std::vector<u8> gzip_decompress(const u8* data, u32 len) {
    if (len < 18) return {};
    if (data[0] != 0x1F || data[1] != 0x8B) return {};

    u8 cm = data[2]; // compression method
    u8 flags = data[3];
    if (cm != 8) return {}; // only deflate supported

    u32 pos = 10; // base header size

    if (flags & 0x04) { // FEXTRA
        if (pos + 2 > len) return {};
        u16 xlen = static_cast<u16>(data[pos]) | (static_cast<u16>(data[pos + 1]) << 8);
        pos += 2 + xlen;
    }
    if (flags & 0x08) { // FNAME
        while (pos < len && data[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x10) { // FCOMMENT
        while (pos < len && data[pos] != 0) pos++;
        pos++;
    }
    if (flags & 0x02) { // FHCRC
        pos += 2;
    }

    if (pos >= len) return {};

    auto decompressed = inflate_internal(data + pos, len - pos);

    return decompressed;
}

async::task<std::vector<u8>> inflate_async(const u8* data, u32 len) {
    co_await async::thread_pool_executor{};
    co_return inflate(data, len);
}

async::task<std::vector<u8>> gzip_decompress_async(const u8* data, u32 len) {
    co_await async::thread_pool_executor{};
    co_return gzip_decompress(data, len);
}

}
