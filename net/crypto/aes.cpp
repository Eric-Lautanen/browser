#include "aes.hpp"

namespace browser::net::crypto {

static const u8 SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u32 RCON[11] = {0, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000};

AES::AES() : nk_(0), nr_(0) {}

void AES::set_key(const u8* key, std::size_t key_len) {
    if (key_len == 16) {
        nk_ = 4;
        nr_ = 10;
    } else if (key_len == 24) {
        nk_ = 6;
        nr_ = 12;
    } else if (key_len == 32) {
        nk_ = 8;
        nr_ = 14;
    } else {
        return;
    }
    key_len_ = key_len;
    std::memcpy(key_, key, key_len_);
    key_expansion();
}

void AES::set_iv(const u8* iv, std::size_t iv_len) {
    iv_len_ = iv_len < 12 ? iv_len : 12;
    std::memcpy(iv_, iv, iv_len_);
}

u32 AES::load_word(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) |
           (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8)  |
           (static_cast<u32>(p[3]));
}

void AES::store_word(u8* p, u32 w) {
    p[0] = static_cast<u8>(w >> 24);
    p[1] = static_cast<u8>(w >> 16);
    p[2] = static_cast<u8>(w >> 8);
    p[3] = static_cast<u8>(w);
}

u32 AES::sub_word(u32 w) {
    return (static_cast<u32>(SBOX[(w >> 24) & 0xFF]) << 24) |
           (static_cast<u32>(SBOX[(w >> 16) & 0xFF]) << 16) |
           (static_cast<u32>(SBOX[(w >> 8) & 0xFF]) << 8)  |
           (static_cast<u32>(SBOX[w & 0xFF]));
}

u32 AES::rot_word(u32 w) {
    return (w << 8) | (w >> 24);
}

u32 AES::xtime(u32 x) {
    return ((x << 1) ^ ((x >> 7) * 0x1B)) & 0xFF;
}

void AES::xor_block(const u8 a[16], const u8 b[16], u8 out[16]) {
    for (int i = 0; i < 16; i++) {
        out[i] = a[i] ^ b[i];
    }
}

void AES::key_expansion() {
    for (std::size_t i = 0; i < nk_; i++) {
        rk_[i] = load_word(key_ + i * 4);
    }

    for (std::size_t i = nk_; i < 4 * (nr_ + 1); i++) {
        u32 temp = rk_[i - 1];
        if (i % nk_ == 0) {
            temp = sub_word(rot_word(temp)) ^ RCON[i / nk_];
        } else if (nk_ == 8 && i % nk_ == 4) {
            temp = sub_word(temp);
        }
        rk_[i] = rk_[i - nk_] ^ temp;
    }
}

void AES::encrypt_block(const u8 in[16], u8 out[16]) const {
    u8 s[4][4];
    for (int c = 0; c < 4; c++) {
        s[0][c] = in[c * 4 + 0];
        s[1][c] = in[c * 4 + 1];
        s[2][c] = in[c * 4 + 2];
        s[3][c] = in[c * 4 + 3];
    }

    auto add_rk = [&](std::size_t r) {
        for (int c = 0; c < 4; c++) {
            u32 w = rk_[r * 4 + c];
            s[0][c] ^= static_cast<u8>(w >> 24);
            s[1][c] ^= static_cast<u8>(w >> 16);
            s[2][c] ^= static_cast<u8>(w >> 8);
            s[3][c] ^= static_cast<u8>(w);
        }
    };

    add_rk(0);
    for (std::size_t r = 1; r < nr_; r++) {
        for (int c = 0; c < 4; c++)
            for (int i = 0; i < 4; i++)
                s[i][c] = SBOX[s[i][c]];

        u8 t = s[1][0]; s[1][0] = s[1][1]; s[1][1] = s[1][2]; s[1][2] = s[1][3]; s[1][3] = t;
        t = s[2][0]; s[2][0] = s[2][2]; s[2][2] = t;
        t = s[2][1]; s[2][1] = s[2][3]; s[2][3] = t;
        t = s[3][3]; s[3][3] = s[3][2]; s[3][2] = s[3][1]; s[3][1] = s[3][0]; s[3][0] = t;

        for (int c = 0; c < 4; c++) {
            u8 a0 = s[0][c], a1 = s[1][c], a2 = s[2][c], a3 = s[3][c];
            s[0][c] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
            s[1][c] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
            s[2][c] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
            s[3][c] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
        }

        add_rk(r);
    }

    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            s[i][c] = SBOX[s[i][c]];

    u8 t = s[1][0]; s[1][0] = s[1][1]; s[1][1] = s[1][2]; s[1][2] = s[1][3]; s[1][3] = t;
    t = s[2][0]; s[2][0] = s[2][2]; s[2][2] = t;
    t = s[2][1]; s[2][1] = s[2][3]; s[2][3] = t;
    t = s[3][3]; s[3][3] = s[3][2]; s[3][2] = s[3][1]; s[3][1] = s[3][0]; s[3][0] = t;

    add_rk(nr_);

    for (int c = 0; c < 4; c++) {
        out[c * 4 + 0] = s[0][c];
        out[c * 4 + 1] = s[1][c];
        out[c * 4 + 2] = s[2][c];
        out[c * 4 + 3] = s[3][c];
    }
}

void AES::gcm_incr(u8* x) const {
    for (int i = 15; i >= 12; i--) {
        if (++x[i] != 0) break;
    }
}

void AES::gf128_mul(const u8 x[16], const u8 y[16], u8 z[16]) const {
    u8 v[16];
    std::memcpy(v, y, 16);
    std::memset(z, 0, 16);

    for (int i = 0; i < 128; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        if (x[byte_idx] & (1 << bit_idx)) {
            xor_block(z, v, z);
        }

        int carry = v[15] & 1;
        for (int j = 15; j > 0; j--) {
            v[j] = (v[j] >> 1) | (v[j - 1] << 7);
        }
        v[0] >>= 1;

        if (carry) {
            v[0] ^= 0xE1;
        }
    }
}

void AES::gcm_ghash(const u8* aad, std::size_t aad_len,
                    const u8* ciphertext, std::size_t ct_len,
                    u8 out[16]) const {
    u8 H[16] = {0};
    encrypt_block(H, H);

    std::vector<u8> input;
    input.reserve(aad_len + 16 + ct_len + 16 + 16);

    input.insert(input.end(), aad, aad + aad_len);
    std::size_t aad_pad = (16 - (aad_len % 16)) % 16;
    input.insert(input.end(), aad_pad, 0);

    input.insert(input.end(), ciphertext, ciphertext + ct_len);
    std::size_t ct_pad = (16 - (ct_len % 16)) % 16;
    input.insert(input.end(), ct_pad, 0);

    u64 aad_bits = static_cast<u64>(aad_len) * 8;
    u64 ct_bits = static_cast<u64>(ct_len) * 8;

    for (int i = 7; i >= 0; i--) input.push_back(static_cast<u8>(aad_bits >> (i * 8)));
    for (int i = 7; i >= 0; i--) input.push_back(static_cast<u8>(ct_bits >> (i * 8)));

    u8 y[16] = {0};
    for (std::size_t i = 0; i < input.size(); i += 16) {
        u8 block[16];
        std::memcpy(block, input.data() + i, 16);
        u8 tmp[16];
        xor_block(y, block, tmp);
        gf128_mul(tmp, H, y);
    }
    std::memcpy(out, y, 16);
}

void AES::gcm_gctr(const u8* icb, const u8* in, std::size_t in_len, u8* out) const {
    u8 cb[16];
    std::memcpy(cb, icb, 16);

    std::size_t offset = 0;
    while (offset < in_len) {
        u8 encrypted[16];
        encrypt_block(cb, encrypted);

        std::size_t remaining = in_len - offset;
        std::size_t block_size = remaining < 16 ? remaining : 16;
        for (std::size_t j = 0; j < block_size; j++) {
            out[offset + j] = in[offset + j] ^ encrypted[j];
        }
        offset += block_size;

        gcm_incr(cb);
    }
}

std::vector<u8> AES::encrypt_gcm(const u8* data, std::size_t len,
                                  const u8* aad, std::size_t aad_len,
                                  u8 tag[16]) {
    u8 J0[16];
    std::memset(J0, 0, 16);
    if (iv_len_ == 12) {
        std::memcpy(J0, iv_, 12);
        J0[15] = 1;
    } else {
        // For non-96-bit IVs, J0 = GHASH(H, IV || 0^s || len(IV)*8)
        u8 H[16] = {0};
        encrypt_block(H, H);
        // Compute GHASH of IV || zero-pad || 8-byte length
        std::vector<u8> iv_input;
        iv_input.insert(iv_input.end(), iv_, iv_ + iv_len_);
        std::size_t iv_pad = (16 - (iv_len_ % 16)) % 16;
        iv_input.insert(iv_input.end(), iv_pad, 0);
        u64 iv_bits = static_cast<u64>(iv_len_) * 8;
        for (int i = 7; i >= 0; i--) iv_input.push_back(static_cast<u8>(iv_bits >> (i * 8)));
        u8 y[16] = {0};
        for (std::size_t i = 0; i < iv_input.size(); i += 16) {
            u8 block[16];
            std::memcpy(block, iv_input.data() + i, 16);
            u8 tmp[16];
            xor_block(y, block, tmp);
            gf128_mul(tmp, H, y);
        }
        std::memcpy(J0, y, 16);
    }

    u8 incr_j0[16];
    std::memcpy(incr_j0, J0, 16);
    gcm_incr(incr_j0);

    std::vector<u8> ct(len);
    gcm_gctr(incr_j0, data, len, ct.data());

    u8 S[16];
    gcm_ghash(aad, aad_len, ct.data(), len, S);

    u8 encrypted_j0[16];
    encrypt_block(J0, encrypted_j0);

    for (int i = 0; i < 16; i++) {
        tag[i] = S[i] ^ encrypted_j0[i];
    }

    return ct;
}

bool AES::decrypt_gcm(const u8* data, std::size_t len,
                      const u8* aad, std::size_t aad_len,
                      const u8 tag[16]) {
    u8 J0[16];
    std::memset(J0, 0, 16);
    if (iv_len_ == 12) {
        std::memcpy(J0, iv_, 12);
        J0[15] = 1;
    } else {
        u8 H[16] = {0};
        encrypt_block(H, H);
        std::vector<u8> iv_input;
        iv_input.insert(iv_input.end(), iv_, iv_ + iv_len_);
        std::size_t iv_pad = (16 - (iv_len_ % 16)) % 16;
        iv_input.insert(iv_input.end(), iv_pad, 0);
        u64 iv_bits = static_cast<u64>(iv_len_) * 8;
        for (int i = 7; i >= 0; i--) iv_input.push_back(static_cast<u8>(iv_bits >> (i * 8)));
        u8 y[16] = {0};
        for (std::size_t i = 0; i < iv_input.size(); i += 16) {
            u8 block[16];
            std::memcpy(block, iv_input.data() + i, 16);
            u8 tmp[16];
            xor_block(y, block, tmp);
            gf128_mul(tmp, H, y);
        }
        std::memcpy(J0, y, 16);
    }

    u8 S[16];
    gcm_ghash(aad, aad_len, data, len, S);

    u8 encrypted_j0[16];
    encrypt_block(J0, encrypted_j0);

    u8 expected_tag[16];
    for (int i = 0; i < 16; i++) {
        expected_tag[i] = S[i] ^ encrypted_j0[i];
    }

    u8 diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= expected_tag[i] ^ tag[i];
    }

    return diff == 0;
}

std::vector<u8> AES::decrypt_gcm_return(const u8* data, std::size_t len,
                                          const u8* aad, std::size_t aad_len,
                                          const u8 tag[16]) {
    u8 J0[16];
    std::memset(J0, 0, 16);
    if (iv_len_ == 12) {
        std::memcpy(J0, iv_, 12);
        J0[15] = 1;
    } else {
        u8 H[16] = {0};
        encrypt_block(H, H);
        std::vector<u8> iv_input;
        iv_input.insert(iv_input.end(), iv_, iv_ + iv_len_);
        std::size_t iv_pad = (16 - (iv_len_ % 16)) % 16;
        iv_input.insert(iv_input.end(), iv_pad, 0);
        u64 iv_bits = static_cast<u64>(iv_len_) * 8;
        for (int i = 7; i >= 0; i--) iv_input.push_back(static_cast<u8>(iv_bits >> (i * 8)));
        u8 y[16] = {0};
        for (std::size_t i = 0; i < iv_input.size(); i += 16) {
            u8 block[16];
            std::memcpy(block, iv_input.data() + i, 16);
            u8 tmp[16];
            xor_block(y, block, tmp);
            gf128_mul(tmp, H, y);
        }
        std::memcpy(J0, y, 16);
    }

    u8 S[16];
    gcm_ghash(aad, aad_len, data, len, S);

    u8 encrypted_j0[16];
    encrypt_block(J0, encrypted_j0);

    u8 expected_tag[16];
    for (int i = 0; i < 16; i++) {
        expected_tag[i] = S[i] ^ encrypted_j0[i];
    }

    u8 diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= expected_tag[i] ^ tag[i];
    }
    if (diff != 0) return {};

    // GCTR decrypt (CTR mode is its own inverse)
    u8 incr_j0[16];
    std::memcpy(incr_j0, J0, 16);
    gcm_incr(incr_j0);

    std::vector<u8> pt(len);
    gcm_gctr(incr_j0, data, len, pt.data());
    return pt;
}

} // namespace browser::net::crypto
