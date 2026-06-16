#include "decoder.hpp"
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>

namespace browser::image {

// Zigzag order: maps linear scan index → natural (raster) DCT coefficient index
static const u8 ZIGZAG[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// --- Huffman table -----------------------------------------------------------

struct HuffTable {
    u8 counts[16] = {};
    std::vector<u8> values;
    u16 min_code[16] = {};
    u16 max_code[16] = {};
    u16 val_offset[16] = {};
    bool valid = false;

    void build() {
        u16 code = 0;
        for (int i = 0; i < 16; i++) {
            if (counts[i]) {
                min_code[i] = code;
                code += counts[i];
                max_code[i] = code - 1;
                val_offset[i] = 0;
                for (int j = 0; j < i; j++) val_offset[i] += counts[j];
            } else {
                min_code[i] = 0xFFFF;
                max_code[i] = 0;
                val_offset[i] = 0;
            }
            code <<= 1;
        }
        valid = true;
    }
};

// --- Bit reader --------------------------------------------------------------

struct BitReader {
    const u8* pos;
    const u8* end;
    int bits_left = 0;
    u32 buf = 0;

    BitReader(const u8* p, const u8* e) : pos(p), end(e) {}

    // Returns 0 or 1 on success, -1 on end-of-data (marker encountered)
    int read_bit() {
        if (bits_left < 0) return -1;
        if (bits_left == 0) {
            if (pos >= end) { bits_left = -1; return -1; }
            u8 byte = *pos++;
            if (byte == 0xFF) {
                if (pos < end && *pos == 0x00) {
                    pos++; // escaped 0xFF
                } else {
                    pos--; // put back the 0xFF for the marker parser
                    bits_left = -1;
                    return -1;
                }
            }
            buf = byte;
            bits_left = 8;
        }
        int bit = (buf >> 7) & 1;
        buf <<= 1;
        bits_left--;
        return bit;
    }

    int read_bits(int n) {
        int val = 0;
        for (int i = 0; i < n; i++) {
            int b = read_bit();
            if (b < 0) return -1;
            val = (val << 1) | b;
        }
        return val;
    }

    int huff_decode(const HuffTable& tbl) {
        if (!tbl.valid) return -1;
        int code = 0;
        for (int i = 0; i < 16; i++) {
            int b = read_bit();
            if (b < 0) return -3;
            code = (code << 1) | b;
            if (code > static_cast<int>(tbl.max_code[i])) continue;
            if (code >= static_cast<int>(tbl.min_code[i])) {
                u32 idx = tbl.val_offset[i] + (code - tbl.min_code[i]);
                if (idx < tbl.values.size()) return tbl.values[idx];
                return -2;
            }
        }
        return -2;
    }

    // Decode a DC/AC value with sign extension
    int decode_extended(int cat, int extra) {
        if (cat == 0) return 0;
        if (extra < (1 << (cat - 1)))
            extra -= (1 << cat) - 1;
        return extra;
    }
};

// --- Per-component state -----------------------------------------------------

struct JComponent {
    u8 id = 0;
    u8 h_samp = 1;
    u8 v_samp = 1;
    u8 q_table_idx = 0;
    u8 dc_tbl = 0;
    u8 ac_tbl = 0;
};

// --- IDCT (separable, float, 8×8) -------------------------------------------

// 1-D IDCT for JPEG: f(x) = sum_{u=0..7} C(u)*F(u)*cos((2x+1)*u*pi/16)
// C(0)=1/√2, C(u>0)=1
// Returns result in out[0..7]
static void idct_1d(const float* in, float* out) {
    static const float C0 = 0.7071067811865475f; // 1/√2
    static const float PI_16 = 3.141592653589793f / 16.0f;
    for (int x = 0; x < 8; x++) {
        float sum = in[0] * C0;
        for (int u = 1; u < 8; u++) {
            sum += in[u] * std::cos((2.0f * x + 1.0f) * u * PI_16);
        }
        out[x] = sum;
    }
}

// 2-D IDCT via row–column separation
// After two 1-D passes the overall scale is the required (1/4).
static void idct_2d(const float* in, float* out) {
    float rows[8][8];
    for (int y = 0; y < 8; y++)
        idct_1d(in + y * 8, rows[y]);

    float col[8];
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) col[y] = rows[y][x];
        idct_1d(col, col); // in-place
        for (int y = 0; y < 8; y++)
            out[y * 8 + x] = col[y] * 0.25f; // final (1/4) factor
    }
}

// --- JPEG decoder -----------------------------------------------------------

class JPEGDecoder : public Decoder {
public:
    Result<Image> decode(const u8* data, size_t size) override {
        data_ = data;
        size_ = size;
        pos_ = 0;

        if (size < 2 || data[0] != 0xFF || data[1] != 0xD8)
            return Result<Image>("JPEG: No SOI marker");
        pos_ = 2;

        std::memset(q_tables_, 0, sizeof(q_tables_));
        for (auto& row : huff_)
            for (auto& t : row) t = HuffTable{};

        comp_count_ = 0;
        h_samp_max_ = 1;
        v_samp_max_ = 1;
        sof_found_ = false;

        parse_markers();

        if (!sof_found_)
            return Result<Image>("JPEG: No SOF marker");
        if (width_ == 0 || height_ == 0 || width_ > 4096 || height_ > 4096)
            return Result<Image>("JPEG: Invalid dimensions");

        // --- decode scan ---
        if (!decode_scan())
            return Result<Image>("JPEG: Failed to decode scan");

        // --- produce RGBA output ---
        return build_output();
    }

private:
    const u8* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;

    // Info from SOF
    int width_ = 0;
    int height_ = 0;
    int comp_count_ = 0;
    bool sof_found_ = false;
    JComponent comps_[4];
    u16 q_tables_[4][64];
    HuffTable huff_[4][2]; // [table_id][class]  class 0=DC, 1=AC

    int h_samp_max_ = 1;
    int v_samp_max_ = 1;

    // Decoded MCU buffer dimensions (padded to MCU grid)
    int mcu_w_ = 0;
    int mcu_h_ = 0;
    std::vector<float> y_buf_;
    std::vector<float> cb_buf_;
    std::vector<float> cr_buf_;

    // ------------------------------------------------------------------
    // Marker parsing
    // ------------------------------------------------------------------

    u8 next_marker() {
        while (pos_ < size_) {
            if (data_[pos_] == 0xFF) {
                pos_++;
                while (pos_ < size_ && data_[pos_] == 0xFF) pos_++;
                if (pos_ < size_ && data_[pos_] != 0x00)
                    return data_[pos_++];
            } else {
                pos_++;
            }
        }
        return 0;
    }

    u16 read_u16() {
        if (pos_ + 2 > size_) return 0;
        u16 v = static_cast<u16>((static_cast<u16>(data_[pos_]) << 8) | data_[pos_ + 1]);
        pos_ += 2;
        return v;
    }

    void skip_segment() {
        if (pos_ + 2 > size_) return;
        u16 len = static_cast<u16>((static_cast<u16>(data_[pos_]) << 8) | data_[pos_ + 1]);
        pos_ += len;
    }

    void parse_markers() {
        while (pos_ < size_) {
            u8 m = next_marker();
            if (m == 0) break;
            switch (m) {
                case 0xE0: case 0xE1: case 0xE2: case 0xE3:
                case 0xE4: case 0xE5: case 0xE6: case 0xE7:
                case 0xE8: case 0xE9: case 0xEA: case 0xEB:
                case 0xEC: case 0xED: case 0xEE: case 0xEF:
                case 0xFE:
                    skip_segment();
                    break;
                case 0xDB:
                    if (!read_dqt()) return;
                    break;
                case 0xC0: case 0xC1:
                    if (!read_sof()) return;
                    break;
                case 0xC4:
                    if (!read_dht()) return;
                    break;
                case 0xDA:
                    if (sof_found_) return; // done parsing headers
                    return;
                case 0xDD: case 0xDC:
                    skip_segment();
                    break;
                case 0xD9:
                    return;
                default:
                    if (pos_ + 1 < size_) {
                        u16 len = read_u16();
                        if (len >= 2) pos_ += len - 2;
                    }
                    break;
            }
        }
    }

    bool read_dqt() {
        u16 len = read_u16();
        if (len < 2) return false;
        size_t end = pos_ + len - 2;
        while (pos_ < end) {
            if (pos_ >= size_) return false;
            u8 info = data_[pos_++];
            u8 prec = (info >> 4) & 0x0F;
            u8 tid = info & 0x0F;
            if (tid >= 4) return false;
            if (prec == 0) {
                if (pos_ + 64 > size_) return false;
                for (int i = 0; i < 64; i++)
                    q_tables_[tid][ZIGZAG[i]] = data_[pos_++];
            } else {
                if (pos_ + 128 > size_) return false;
                for (int i = 0; i < 64; i++) {
                    q_tables_[tid][ZIGZAG[i]] = static_cast<u16>(
                        (static_cast<u16>(data_[pos_]) << 8) | data_[pos_ + 1]);
                    pos_ += 2;
                }
            }
        }
        return true;
    }

    bool read_sof() {
        u16 len = read_u16();
        if (len < 6) return false;
        u8 prec = data_[pos_++];
        if (prec != 8) return false;
        height_ = static_cast<int>(read_u16());
        width_ = static_cast<int>(read_u16());
        comp_count_ = data_[pos_++];
        sof_found_ = true;
        if (comp_count_ < 1 || comp_count_ > 4) return false;
        if (pos_ + comp_count_ * 3 > size_) return false;
        for (int i = 0; i < comp_count_; i++) {
            comps_[i].id = data_[pos_++];
            u8 s = data_[pos_++];
            comps_[i].h_samp = (s >> 4) & 0x0F;
            comps_[i].v_samp = s & 0x0F;
            comps_[i].q_table_idx = data_[pos_++];
            if (comps_[i].h_samp > h_samp_max_) h_samp_max_ = comps_[i].h_samp;
            if (comps_[i].v_samp > v_samp_max_) v_samp_max_ = comps_[i].v_samp;
        }
        return true;
    }

    bool read_dht() {
        u16 len = read_u16();
        if (len < 2) return false;
        size_t end = pos_ + len - 2;
        while (pos_ < end) {
            if (pos_ >= size_) return false;
            u8 info = data_[pos_++];
            u8 cls = (info >> 4) & 0x0F;
            u8 tid = info & 0x0F;
            if (cls > 1 || tid > 3) return false;
            auto& tbl = huff_[tid][cls];
            u32 total = 0;
            for (int i = 0; i < 16; i++) {
                tbl.counts[i] = data_[pos_++];
                total += tbl.counts[i];
            }
            if (total > 256 || pos_ + total > size_) return false;
            tbl.values.resize(total);
            for (u32 i = 0; i < total; i++)
                tbl.values[i] = data_[pos_++];
            tbl.build();
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Scan decoding
    // ------------------------------------------------------------------

    bool decode_scan() {
        // --- read SOS header using pos_ ---
        u16 sos_len = read_u16();
        if (sos_len < 4) return false;
        int num_sc = data_[pos_++];
        if (num_sc < 1 || num_sc > 4) return false;
        // Baseline JPEG requires all components in one scan
        if (num_sc != comp_count_) return false;
        // SOS segment must contain: 1(nc) + 2*nc + 3(spectral) bytes after length
        size_t sos_payload = static_cast<size_t>(sos_len) - 2;
        size_t expected = static_cast<size_t>(1 + 2 * num_sc + 3);
        if (sos_payload < expected) return false;

        for (int i = 0; i < num_sc; i++) {
            if (pos_ + 2 > size_) return false;
            u8 cid = data_[pos_++];
            u8 tbl = data_[pos_++];
            for (int j = 0; j < comp_count_; j++) {
                if (comps_[j].id == cid) {
                    comps_[j].dc_tbl = (tbl >> 4) & 0x0F;
                    comps_[j].ac_tbl = tbl & 0x0F;
                    break;
                }
            }
        }
        // spectral selection (3 bytes) — only baseline (0,63,0) is handled
        if (pos_ + 3 > size_) return false;
        pos_ += 3;

        // --- bit reader starts at current position ---
        BitReader br(data_ + pos_, data_ + size_);

        int mcu_rows = (height_ + v_samp_max_ * 8 - 1) / (v_samp_max_ * 8);
        int mcu_cols = (width_ + h_samp_max_ * 8 - 1) / (h_samp_max_ * 8);
        mcu_w_ = mcu_cols * h_samp_max_ * 8;
        mcu_h_ = mcu_rows * v_samp_max_ * 8;

        y_buf_.resize(static_cast<size_t>(mcu_w_) * mcu_h_, 0.0f);
        if (comp_count_ >= 3) {
            int cs = mcu_w_ / h_samp_max_;
            int rs = mcu_h_ / v_samp_max_;
            cb_buf_.resize(static_cast<size_t>(cs) * rs, 0.0f);
            cr_buf_.resize(static_cast<size_t>(cs) * rs, 0.0f);
        }

        float block[64] = {};
        int dc_pred[4] = {};

        for (int my = 0; my < mcu_rows; my++) {
            for (int mx = 0; mx < mcu_cols; mx++) {
                for (int c = 0; c < comp_count_; c++) {
                    auto& ci = comps_[c];
                    auto& hdc = huff_[ci.dc_tbl][0];
                    auto& hac = huff_[ci.ac_tbl][1];

                    for (int vb = 0; vb < ci.v_samp; vb++) {
                        for (int hb = 0; hb < ci.h_samp; hb++) {
                            std::memset(block, 0, sizeof(block));

                            // --- DC ---
                            int cat = br.huff_decode(hdc);
                            if (cat < 0) return false;
                            if (cat > 0) {
                                int extra = br.read_bits(cat);
                                if (extra < 0) return false;
                                dc_pred[c] += br.decode_extended(cat, extra);
                            }
                            block[0] = static_cast<float>(dc_pred[c]);

                            // --- AC ---
                            int bi = 1;
                            bool ac_ok = true;
                            while (bi < 64) {
                                int val = br.huff_decode(hac);
                                if (val == -3) { break; }    // end of data (EOI/restart)
                                if (val == -2 || val == -1) { ac_ok = false; break; } // table error
                                if (val == 0x00) break; // EOB
                                int run = (val >> 4) & 0x0F;
                                int sz = val & 0x0F;
                                bi += run;
                                if (bi >= 64) break;
                                if (sz > 0) {
                                    int extra = br.read_bits(sz);
                                    if (extra < 0) { ac_ok = false; break; }
                                    block[ZIGZAG[bi]] = static_cast<float>(br.decode_extended(sz, extra));
                                    bi++;
                                } else if (run == 0x0F) {
                                    bi += 16; // ZRL
                                }
                            }
                            if (!ac_ok) return false;

                            // --- dequantize ---
                            u16* qt = q_tables_[ci.q_table_idx];
                            for (int i = 0; i < 64; i++)
                                block[i] *= static_cast<float>(qt[i]);

                            // --- IDCT ---
                            float idct[64];
                            idct_2d(block, idct);

                            // --- store to component buffer ---
                            int bx0 = mx * h_samp_max_ * 8 + hb * 8;
                            int by0 = my * v_samp_max_ * 8 + vb * 8;
                            int y_stride = mcu_w_;

                            if (c == 0) {
                                for (int yy = 0; yy < 8; yy++) {
                                    for (int xx = 0; xx < 8; xx++) {
                                        int px = bx0 + xx;
                                        int py = by0 + yy;
                                        if (px < mcu_w_ && py < mcu_h_)
                                            y_buf_[py * y_stride + px] = idct[yy * 8 + xx] + 128.0f;
                                    }
                                }
                            } else if (c >= 1 && c <= 2) {
                                // Chroma: use component's own sampling factors for stride/index
                                auto* buf = (c == 1) ? &cb_buf_ : &cr_buf_;
                                if (buf->empty()) continue;
                                int ch_stride = mcu_w_ / ci.h_samp;
                                int ch_x0 = bx0 / ci.h_samp;
                                int ch_y0 = by0 / ci.v_samp;
                                int ch_h = mcu_h_ / ci.v_samp;
                                for (int yy = 0; yy < 8; yy++) {
                                    for (int xx = 0; xx < 8; xx++) {
                                        int px = ch_x0 + xx;
                                        int py = ch_y0 + yy;
                                        if (px < ch_stride && py < ch_h)
                                            (*buf)[py * ch_stride + px] = idct[yy * 8 + xx] + 128.0f;
                                    }
                                }
                            }
                        } // hb
                    }     // vb
                }         // components
            }             // mx
        }                 // my
        return true;
    }

    // ------------------------------------------------------------------
    // Output assembly
    // ------------------------------------------------------------------

    static u8 clamp_u8(float v) {
        if (v < 0.0f) return 0;
        if (v > 255.0f) return 255;
        return static_cast<u8>(v + 0.5f);
    }

    Result<Image> build_output() {
        Image img;
        img.width = static_cast<u32>(width_);
        img.height = static_cast<u32>(height_);
        img.format = ImageFormat::JPEG;
        img.rgba_pixels.resize(img.width * img.height * 4);

        int y_stride = mcu_w_;

        if (comp_count_ == 1) {
            // Grayscale: Y → RGB
            for (int y = 0; y < height_; y++) {
                for (int x = 0; x < width_; x++) {
                    u8 v = clamp_u8(y_buf_[y * y_stride + x]);
                    size_t off = (static_cast<size_t>(y) * img.width + x) * 4;
                    img.rgba_pixels[off + 0] = v;
                    img.rgba_pixels[off + 1] = v;
                    img.rgba_pixels[off + 2] = v;
                    img.rgba_pixels[off + 3] = 255;
                }
            }
            return Result<Image>(std::move(img));
        }

        // YCbCr → RGB (nearest-neighbour chroma upsampling)
        int cb_stride = mcu_w_ / h_samp_max_;
        int ratio_h = h_samp_max_;
        int ratio_v = v_samp_max_;

        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                float yy = y_buf_[y * y_stride + x];
                float cb = cb_buf_[(y / ratio_v) * cb_stride + (x / ratio_h)];
                float cr = cr_buf_[(y / ratio_v) * cb_stride + (x / ratio_h)];

                float r = yy + 1.402f * (cr - 128.0f);
                float g = yy - 0.344136f * (cb - 128.0f) - 0.714136f * (cr - 128.0f);
                float b = yy + 1.772f * (cb - 128.0f);

                size_t off = (static_cast<size_t>(y) * img.width + x) * 4;
                img.rgba_pixels[off + 0] = clamp_u8(r);
                img.rgba_pixels[off + 1] = clamp_u8(g);
                img.rgba_pixels[off + 2] = clamp_u8(b);
                img.rgba_pixels[off + 3] = 255;
            }
        }
        return Result<Image>(std::move(img));
    }
};

std::unique_ptr<Decoder> create_jpeg_decoder() {
    return std::make_unique<JPEGDecoder>();
}

} // namespace browser::image
