#include "poly1305.hpp"

namespace browser::net::crypto {

Poly1305::Poly1305() {
    std::memset(r_, 0, sizeof(r_));
    std::memset(s_, 0, sizeof(s_));
    std::memset(h_, 0, sizeof(h_));
    std::memset(buf_, 0, sizeof(buf_));
}

u32 Poly1305::load32_le(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

void Poly1305::set_key(const u8 key[32]) {
    // Standard Poly1305 clamping of the first 16 key bytes.
    u8 rbytes[16];
    std::memcpy(rbytes, key, 16);
    rbytes[3] &= 15;
    rbytes[7] &= 15;
    rbytes[11] &= 15;
    rbytes[15] &= 15;
    rbytes[4] &= 252;
    rbytes[8] &= 252;
    rbytes[12] &= 252;

    // Decompose clamped r into five 26-bit limbs. Clamped r still reaches
    // bit ~123, so FOUR limbs cannot hold it — the previous four-limb code
    // silently dropped r's high bits and produced wrong tags for every
    // message longer than one block.
    u32 w0 = load32_le(rbytes + 0);
    u32 w1 = load32_le(rbytes + 4);
    u32 w2 = load32_le(rbytes + 8);
    u32 w3 = load32_le(rbytes + 12);

    r_[0] = w0 & 0x3FFFFFF;
    r_[1] = ((w1 << 6) | (w0 >> 26)) & 0x3FFFFFF;
    r_[2] = ((w2 << 12) | (w1 >> 20)) & 0x3FFFFFF;
    r_[3] = ((w3 << 18) | (w2 >> 14)) & 0x3FFFFFF;
    r_[4] = w3 >> 8;

    s_[0] = load32_le(key + 16);
    s_[1] = load32_le(key + 20);
    s_[2] = load32_le(key + 24);
    s_[3] = load32_le(key + 28);
}

void Poly1305::process_block(u32 hibit) {
    // Message block decomposed with the same limb layout, plus the mandatory
    // 2^128 padding bit in limb 4.
    u32 m0 = load32_le(buf_ + 0);
    u32 m1 = load32_le(buf_ + 4);
    u32 m2 = load32_le(buf_ + 8);
    u32 m3 = load32_le(buf_ + 12);

    u32 n0 = m0 & 0x3FFFFFF;
    u32 n1 = (m0 >> 26) | ((m1 & 0xFFFFF) << 6);
    u32 n2 = (m1 >> 20) | ((m2 & 0x3FFF) << 12);
    u32 n3 = (m2 >> 14) | ((m3 & 0xFF) << 18);
    u32 n4 = ((m3 >> 8) & 0xFFFFFF) | hibit;

    u64 c = 0;
    h_[0] += n0; c = h_[0] >> 26; h_[0] &= 0x3FFFFFF;
    h_[1] += n1 + (u32)c; c = h_[1] >> 26; h_[1] &= 0x3FFFFFF;
    h_[2] += n2 + (u32)c; c = h_[2] >> 26; h_[2] &= 0x3FFFFFF;
    h_[3] += n3 + (u32)c; c = h_[3] >> 26; h_[3] &= 0x3FFFFFF;
    h_[4] += n4 + (u32)c; c = h_[4] >> 26; h_[4] &= 0x3FFFFFF;
    h_[0] += (u32)c * 5;
    c = h_[0] >> 26; h_[0] &= 0x3FFFFFF;
    h_[1] += (u32)c;

    // Schoolbook h *= r over five limbs; products that wrap past limb 4 are
    // folded back with a factor of 5 (2^130 ≡ 5 mod 2^130 - 5).
    u64 h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];
    u64 r0 = r_[0], r1 = r_[1], r2 = r_[2], r3 = r_[3], r4 = r_[4];
    u64 s1 = 5 * r1, s2 = 5 * r2, s3 = 5 * r3, s4 = 5 * r4;

    u64 d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
    u64 d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
    u64 d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
    u64 d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
    u64 d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

    c = d0 >> 26; d0 &= 0x3FFFFFF; d1 += c;
    c = d1 >> 26; d1 &= 0x3FFFFFF; d2 += c;
    c = d2 >> 26; d2 &= 0x3FFFFFF; d3 += c;
    c = d3 >> 26; d3 &= 0x3FFFFFF; d4 += c;
    c = d4 >> 26; d4 &= 0x3FFFFFF; d0 += c * 5;
    c = d0 >> 26; d0 &= 0x3FFFFFF; d1 += c;

    h_[0] = (u32)d0; h_[1] = (u32)d1; h_[2] = (u32)d2; h_[3] = (u32)d3; h_[4] = (u32)d4;
}

void Poly1305::update(const u8* data, std::size_t len) {
    for (std::size_t i = 0; i < len; i++) {
        buf_[buf_len_++] = data[i];
        if (buf_len_ == 16) {
            process_block(1u << 24);
            buf_len_ = 0;
        }
    }
}

void Poly1305::finish(u8 mac[16]) {
    if (buf_len_ > 0) {
        // The end-marker byte lands at bit 8*len inside the normal
        // decomposition, so the tail block is processed with hibit = 0.
        buf_[buf_len_] = 1;
        for (std::size_t i = buf_len_ + 1; i < 16; i++)
            buf_[i] = 0;
        process_block(0);
    }

    // Full carry propagation.
    u64 c = 0;
    c = h_[0] >> 26; h_[0] &= 0x3FFFFFF; h_[1] += (u32)c;
    c = h_[1] >> 26; h_[1] &= 0x3FFFFFF; h_[2] += (u32)c;
    c = h_[2] >> 26; h_[2] &= 0x3FFFFFF; h_[3] += (u32)c;
    c = h_[3] >> 26; h_[3] &= 0x3FFFFFF; h_[4] += (u32)c;
    c = h_[4] >> 26; h_[4] &= 0x3FFFFFF; h_[0] += (u32)c * 5;
    c = h_[0] >> 26; h_[0] &= 0x3FFFFFF; h_[1] += (u32)c;
    c = h_[1] >> 26; h_[1] &= 0x3FFFFFF; h_[2] += (u32)c;

    // h < 2^130, so h >= p holds only for the five values p..p+4 — detect
    // them by their exact limb pattern and subtract p once.
    if (h_[4] == 0x3FFFFFF && h_[3] == 0x3FFFFFF &&
        h_[2] == 0x3FFFFFF && h_[1] == 0x3FFFFFF &&
        h_[0] >= 0x3FFFFFB) {
        h_[0] -= 0x3FFFFFB;
        h_[1] = h_[2] = h_[3] = h_[4] = 0;
    }

    u32 w0 = h_[0] | (h_[1] << 26);
    u32 w1 = (h_[1] >> 6) | (h_[2] << 20);
    u32 w2 = (h_[2] >> 12) | (h_[3] << 14);
    u32 w3 = (h_[3] >> 18) | (h_[4] << 8);

    u64 carry = 0;
    carry = (u64)w0 + s_[0]; w0 = (u32)carry; carry >>= 32;
    carry = (u64)w1 + s_[1] + carry; w1 = (u32)carry; carry >>= 32;
    carry = (u64)w2 + s_[2] + carry; w2 = (u32)carry; carry >>= 32;
    carry = (u64)w3 + s_[3] + carry; w3 = (u32)carry;

    mac[0] = (u8)(w0); mac[1] = (u8)(w0 >> 8); mac[2] = (u8)(w0 >> 16); mac[3] = (u8)(w0 >> 24);
    mac[4] = (u8)(w1); mac[5] = (u8)(w1 >> 8); mac[6] = (u8)(w1 >> 16); mac[7] = (u8)(w1 >> 24);
    mac[8] = (u8)(w2); mac[9] = (u8)(w2 >> 8); mac[10] = (u8)(w2 >> 16); mac[11] = (u8)(w2 >> 24);
    mac[12] = (u8)(w3); mac[13] = (u8)(w3 >> 8); mac[14] = (u8)(w3 >> 16); mac[15] = (u8)(w3 >> 24);
}

} // namespace browser::net::crypto
