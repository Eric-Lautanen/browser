#include "connection.hpp"

#include <algorithm>
#include <cstring>

namespace browser::net::http2 {

    struct HuffmanSymbol {
        u32 code;
        u8 bits;
        u16 symbol;
    };

    static const HuffmanSymbol kHuffmanTable[257] = {
#include "../huffman_table.inc"
    };

    struct HuffmanNode {
        u16 symbol;
        HuffmanNode *child[2];

        HuffmanNode() : symbol(256) { child[0] = child[1] = nullptr; }
    };

    static HuffmanNode *build_huffman_trie() {
        auto *root = new HuffmanNode();
        for (int sym = 0; sym < 256; sym++) {
            u32 code = kHuffmanTable[sym].code;
            u8 bits = kHuffmanTable[sym].bits;
            if (bits == 0)
                continue;
            HuffmanNode *node = root;
            for (int b = static_cast<int>(bits) - 1; b >= 0; b--) {
                u8 bit = static_cast<u8>((code >> b) & 1);
                if (!node->child[bit])
                    node->child[bit] = new HuffmanNode();
                node = node->child[bit];
            }
            node->symbol = static_cast<u16>(sym);
        }
        return root;
    }

    static HuffmanNode *get_huffman_trie() {
        static auto *trie = build_huffman_trie();
        return trie;
    }

    std::string HPack::huffman_decode(const u8 *data, u32 len) {
        if (len == 0)
            return {};
        std::string result;
        auto *trie = get_huffman_trie();
        u64 bit_pos = 0;
        u64 total_bits = static_cast<u64>(len) * 8;
        while (bit_pos < total_bits) {
            HuffmanNode *node = trie;
            u64 start_pos = bit_pos;
            while (bit_pos < total_bits) {
                u64 byte_off = bit_pos >> 3;
                u8 bit_off = static_cast<u8>(bit_pos & 7);
                u8 bit = static_cast<u8>((data[byte_off] >> (7 - bit_off)) & 1);
                bit_pos++;
                node = node->child[bit];
                if (!node) {
                    u64 padding_bits = total_bits - start_pos;
                    if (padding_bits > 7)
                        return {};
                    for (u64 b = 0; b < padding_bits; b++) {
                        u64 bo = (start_pos + b) >> 3;
                        u8 bi = static_cast<u8>((start_pos + b) & 7);
                        u8 val = static_cast<u8>((data[bo] >> (7 - bi)) & 1);
                        if (val == 0)
                            return {};
                    }
                    return result;
                }
                if (node->symbol <= 255) {
                    result += static_cast<char>(node->symbol);
                    goto next;
                }
            }
            {
                u64 padding_bits = total_bits - start_pos;
                if (padding_bits > 7)
                    return {};
                for (u64 b = 0; b < padding_bits; b++) {
                    u64 bo = (start_pos + b) >> 3;
                    u8 bi = static_cast<u8>((start_pos + b) & 7);
                    u8 val = static_cast<u8>((data[bo] >> (7 - bi)) & 1);
                    if (val == 0)
                        return {};
                }
                return result;
            }
        next:;
        }
        return result;
    }

    const HPackEntry HPack::kStaticTable[61] = {{":authority", ""},
                                                {":method", "GET"},
                                                {":method", "POST"},
                                                {":path", "/"},
                                                {":path", "/index.html"},
                                                {":scheme", "http"},
                                                {":scheme", "https"},
                                                {":status", "200"},
                                                {":status", "204"},
                                                {":status", "206"},
                                                {":status", "304"},
                                                {":status", "400"},
                                                {":status", "404"},
                                                {":status", "500"},
                                                {"accept-charset", ""},
                                                {"accept-encoding", ""},
                                                {"accept-language", ""},
                                                {"accept-ranges", ""},
                                                {"accept", ""},
                                                {"access-control-allow-origin", ""},
                                                {"age", ""},
                                                {"allow", ""},
                                                {"authorization", ""},
                                                {"cache-control", ""},
                                                {"content-disposition", ""},
                                                {"content-encoding", ""},
                                                {"content-language", ""},
                                                {"content-length", ""},
                                                {"content-location", ""},
                                                {"content-range", ""},
                                                {"content-type", ""},
                                                {"cookie", ""},
                                                {"date", ""},
                                                {"etag", ""},
                                                {"expect", ""},
                                                {"expires", ""},
                                                {"from", ""},
                                                {"host", ""},
                                                {"if-match", ""},
                                                {"if-modified-since", ""},
                                                {"if-none-match", ""},
                                                {"if-range", ""},
                                                {"if-unmodified-since", ""},
                                                {"last-modified", ""},
                                                {"link", ""},
                                                {"location", ""},
                                                {"max-forwards", ""},
                                                {"proxy-authenticate", ""},
                                                {"proxy-authorization", ""},
                                                {"range", ""},
                                                {"referer", ""},
                                                {"refresh", ""},
                                                {"retry-after", ""},
                                                {"server", ""},
                                                {"set-cookie", ""},
                                                {"strict-transport-security", ""},
                                                {"transfer-encoding", ""},
                                                {"user-agent", ""},
                                                {"vary", ""},
                                                {"via", ""},
                                                {"www-authenticate", ""}};

    u32 HPack::decode_integer(const u8 *data, u32 len, u32 &pos, u8 prefix_bits) {
        if (pos >= len)
            return 0;
        u8 prefix_mask = static_cast<u8>((1 << prefix_bits) - 1);
        u32 value = data[pos] & prefix_mask;
        if (value < static_cast<u32>(prefix_mask)) {
            pos++;
            return value;
        }
        pos++;
        u32 shift = 0;
        u32 cont_bytes = 0;
        while (true) {
            if (pos >= len)
                return value;
            if (cont_bytes >= 5)
                return value;
            u8 b = data[pos];
            value += static_cast<u32>(b & 0x7F) << shift;
            shift += 7;
            pos++;
            cont_bytes++;
            if (!(b & 0x80))
                break;
        }
        return value;
    }

    std::vector<u8> HPack::encode_integer(u32 value, u8 prefix_bits) {
        std::vector<u8> out;
        u8 prefix_mask = static_cast<u8>((1 << prefix_bits) - 1);
        if (value < static_cast<u32>(prefix_mask)) {
            out.push_back(static_cast<u8>(value));
        } else {
            out.push_back(prefix_mask);
            value -= prefix_mask;
            while (value >= 128) {
                out.push_back(static_cast<u8>((value & 0x7F) | 0x80));
                value >>= 7;
            }
            out.push_back(static_cast<u8>(value & 0x7F));
        }
        return out;
    }

    std::string HPack::decode_string(const u8 *data, u32 len, u32 &pos) {
        if (pos >= len)
            return {};
        u8 first = data[pos];
        bool huffman = (first & 0x80) != 0;
        u32 str_len = decode_integer(data, len, pos, 7);
        if (pos + str_len > len) {
            pos = len;
            return {};
        }
        std::string result;
        if (huffman) {
            result = huffman_decode(data + pos, str_len);
        } else {
            result.assign(reinterpret_cast<const char *>(data + pos), str_len);
        }
        pos += str_len;
        return result;
    }

    std::vector<u8> HPack::encode_string(const std::string &s) {
        auto len_enc = encode_integer(static_cast<u32>(s.size()), 7);
        std::vector<u8> out;
        out.push_back(len_enc[0]);
        for (std::size_t i = 1; i < len_enc.size(); i++) out.push_back(len_enc[i]);
        out.insert(out.end(), s.begin(), s.end());
        return out;
    }

    const HPackEntry *HPack::get_entry(u32 index) const {
        if (index == 0)
            return nullptr;
        if (index <= 61)
            return &kStaticTable[index - 1];
        u32 dyn_idx = index - 62;
        if (dyn_idx < dynamic_table_.size())
            return &dynamic_table_[dyn_idx];
        return nullptr;
    }

    u32 HPack::find_name_in_table(const std::string &name) const {
        for (u32 i = 0; i < 61; i++) {
            if (kStaticTable[i].name == name)
                return i + 1;
        }
        for (u32 i = 0; i < dynamic_table_.size(); i++) {
            if (dynamic_table_[i].name == name)
                return 62 + i;
        }
        return 0;
    }

    u32 HPack::find_in_table(const std::string &name, const std::string &value) const {
        for (u32 i = 0; i < 61; i++) {
            if (kStaticTable[i].name == name && kStaticTable[i].value == value)
                return i + 1;
        }
        for (u32 i = 0; i < dynamic_table_.size(); i++) {
            if (dynamic_table_[i].name == name && dynamic_table_[i].value == value)
                return 62 + i;
        }
        return 0;
    }

    void HPack::evict_to_fit(u32 new_entry_size) {
        u32 max_size = max_table_size_;
        while (!dynamic_table_.empty() && current_table_size_ + new_entry_size > max_size) {
            auto &last = dynamic_table_.back();
            u32 entry_size = static_cast<u32>(last.name.size() + last.value.size() + 32);
            current_table_size_ -= entry_size < current_table_size_ ? entry_size : current_table_size_;
            dynamic_table_.pop_back();
        }
    }

    HPack::HPack() = default;
    HPack::~HPack() = default;
    HPack::HPack(HPack &&) noexcept = default;
    HPack &HPack::operator=(HPack &&) noexcept = default;

    void HPack::set_max_table_size(u32 size) {
        max_table_size_ = size;
        while (current_table_size_ > max_table_size_ && !dynamic_table_.empty()) {
            auto &last = dynamic_table_.back();
            u32 entry_size = static_cast<u32>(last.name.size() + last.value.size() + 32);
            current_table_size_ -= entry_size < current_table_size_ ? entry_size : current_table_size_;
            dynamic_table_.pop_back();
        }
    }

    std::vector<HPackEntry> HPack::decode(const u8 *data, u32 len) {
        std::vector<HPackEntry> entries;
        u32 pos = 0;
        while (pos < len) {
            u8 first = data[pos];
            if (first & 0x80) {
                u32 idx = decode_integer(data, len, pos, 7);
                if (idx == 0)
                    break;
                auto *entry = get_entry(idx);
                if (!entry)
                    break;
                entries.push_back(*entry);
            } else if ((first & 0xC0) == 0x40) {
                u32 name_idx = decode_integer(data, len, pos, 6);
                std::string name, value;
                if (name_idx > 0) {
                    auto *entry = get_entry(name_idx);
                    if (!entry)
                        break;
                    name = entry->name;
                } else {
                    name = decode_string(data, len, pos);
                }
                value = decode_string(data, len, pos);
                entries.push_back({name, value});
                u32 entry_size = static_cast<u32>(name.size() + value.size() + 32);
                evict_to_fit(entry_size);
                if (current_table_size_ + entry_size <= max_table_size_) {
                    dynamic_table_.insert(dynamic_table_.begin(), {name, value});
                    current_table_size_ += entry_size;
                }
            } else if ((first & 0xF0) == 0x00) {
                u32 name_idx = decode_integer(data, len, pos, 4);
                std::string name, value;
                if (name_idx > 0) {
                    auto *entry = get_entry(name_idx);
                    if (!entry)
                        break;
                    name = entry->name;
                } else {
                    name = decode_string(data, len, pos);
                }
                value = decode_string(data, len, pos);
                entries.push_back({name, value});
            } else if ((first & 0xF0) == 0x10) {
                u32 name_idx = decode_integer(data, len, pos, 4);
                std::string name, value;
                if (name_idx > 0) {
                    auto *entry = get_entry(name_idx);
                    if (!entry)
                        break;
                    name = entry->name;
                } else {
                    name = decode_string(data, len, pos);
                }
                value = decode_string(data, len, pos);
                entries.push_back({name, value});
            } else if ((first & 0xE0) == 0x20) {
                u32 new_size = decode_integer(data, len, pos, 5);
                set_max_table_size(new_size);
            } else {
                break;
            }
        }
        return entries;
    }

    std::vector<u8> HPack::encode(const std::vector<HPackEntry> &headers) {
        std::vector<u8> out;
        for (auto &h : headers) {
            u32 table_idx = find_in_table(h.name, h.value);
            if (table_idx > 0) {
                auto idx_enc = encode_integer(table_idx, 7);
                out.push_back(idx_enc[0] | 0x80);
                for (std::size_t i = 1; i < idx_enc.size(); i++) out.push_back(idx_enc[i]);
            } else {
                u32 name_idx = find_name_in_table(h.name);
                if (name_idx > 0) {
                    auto idx_enc = encode_integer(name_idx, 6);
                    out.push_back(idx_enc[0] | 0x40);
                    for (std::size_t i = 1; i < idx_enc.size(); i++) out.push_back(idx_enc[i]);
                } else {
                    auto idx_enc = encode_integer(0, 6);
                    out.push_back(idx_enc[0] | 0x40);
                    for (std::size_t i = 1; i < idx_enc.size(); i++) out.push_back(idx_enc[i]);
                    auto name_enc = encode_string(h.name);
                    out.insert(out.end(), name_enc.begin(), name_enc.end());
                }
                auto val_enc = encode_string(h.value);
                out.insert(out.end(), val_enc.begin(), val_enc.end());
                u32 entry_size = static_cast<u32>(h.name.size() + h.value.size() + 32);
                evict_to_fit(entry_size);
                if (current_table_size_ + entry_size <= max_table_size_) {
                    dynamic_table_.insert(dynamic_table_.begin(), h);
                    current_table_size_ += entry_size;
                }
            }
        }
        return out;
    }

}  // namespace browser::net::http2
