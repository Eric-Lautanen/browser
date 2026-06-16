#include "chacha20.hpp"

namespace browser::net::crypto {

static u32 load32_le(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

ChaCha20::ChaCha20() {
    std::memset(key_, 0, sizeof(key_));
    std::memset(nonce_, 0, sizeof(nonce_));
}

void ChaCha20::set_key(const u8 key[32]) {
    std::memcpy(key_, key, 32);
}

void ChaCha20::set_nonce(const u8 nonce[12]) {
    std::memcpy(nonce_, nonce, 12);
}

void ChaCha20::set_counter(u32 counter) {
    counter_ = counter;
}

u32 ChaCha20::rotl(u32 x, int n) {
    return (x << n) | (x >> (32 - n));
}

void ChaCha20::quarter_round(u32& a, u32& b, u32& c, u32& d) {
    a += b; d ^= a; d = rotl(d, 16);
    c += d; b ^= c; b = rotl(b, 12);
    a += b; d ^= a; d = rotl(d, 8);
    c += d; b ^= c; b = rotl(b, 7);
}

void ChaCha20::next_block() {
    state_[0] = 0x61707865;
    state_[1] = 0x3320646e;
    state_[2] = 0x79622d32;
    state_[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state_[4 + i] = load32_le(key_ + i * 4);
    state_[12] = counter_;
    for (int i = 0; i < 3; i++)
        state_[13 + i] = load32_le(nonce_ + i * 4);

    std::memcpy(block_, state_, sizeof(block_));

    for (int round = 0; round < 10; round++) {
        quarter_round(block_[0], block_[4], block_[8], block_[12]);
        quarter_round(block_[1], block_[5], block_[9], block_[13]);
        quarter_round(block_[2], block_[6], block_[10], block_[14]);
        quarter_round(block_[3], block_[7], block_[11], block_[15]);
        quarter_round(block_[0], block_[5], block_[10], block_[15]);
        quarter_round(block_[1], block_[6], block_[11], block_[12]);
        quarter_round(block_[2], block_[7], block_[8], block_[13]);
        quarter_round(block_[3], block_[4], block_[9], block_[14]);
    }

    for (int i = 0; i < 16; i++)
        block_[i] += state_[i];

    has_block_ = true;
    block_pos_ = 0;
    counter_++;
}

void ChaCha20::encrypt(const u8* in, std::size_t len, u8* out) {
    for (std::size_t i = 0; i < len; i++) {
        if (!has_block_ || block_pos_ == 64)
            next_block();
        u32 word_idx = block_pos_ / 4;
        u32 byte_in_word = block_pos_ % 4;
        u8 key_byte = static_cast<u8>((block_[word_idx] >> (byte_in_word * 8)) & 0xFF);
        out[i] = in[i] ^ key_byte;
        block_pos_++;
    }
}

} // namespace browser::net::crypto
