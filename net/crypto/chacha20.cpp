#include "chacha20.hpp"

namespace browser::net::crypto {

    static u32 load32_le(const u8 *p) {
        return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
               (static_cast<u32>(p[3]) << 24);
    }

    static void store32_le(u8 *p, u32 v) {
        p[0] = static_cast<u8>(v);
        p[1] = static_cast<u8>(v >> 8);
        p[2] = static_cast<u8>(v >> 16);
        p[3] = static_cast<u8>(v >> 24);
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

    void ChaCha20::quarter_round(u32 &a, u32 &b, u32 &c, u32 &d) {
        a += b;
        d ^= a;
        d = rotl(d, 16);
        c += d;
        b ^= c;
        b = rotl(b, 12);
        a += b;
        d ^= a;
        d = rotl(d, 8);
        c += d;
        b ^= c;
        b = rotl(b, 7);
    }

    void ChaCha20::next_block() {
        state_[0] = 0x61707865;
        state_[1] = 0x3320646e;
        state_[2] = 0x79622d32;
        state_[3] = 0x6b206574;
        for (int i = 0; i < 8; i++) state_[4 + i] = load32_le(key_ + i * 4);
        state_[12] = counter_;
        for (int i = 0; i < 3; i++) state_[13 + i] = load32_le(nonce_ + i * 4);

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

        for (int i = 0; i < 16; i++) block_[i] += state_[i];

        has_block_ = true;
        block_pos_ = 0;
        counter_++;
    }

    void ChaCha20::encrypt(const u8 *in, std::size_t len, u8 *out) {
        // N-P1: the keystream was previously extracted one byte at a time with
        // a div/mod per byte. XOR whole keystream words instead, tracking only
        // the byte offset within the 64-byte block; bytes tail off one at a time.
        std::size_t i = 0;
        while (i < len) {
            if (!has_block_ || block_pos_ >= 64)
                next_block();

            u32 word_idx = block_pos_ >> 2;
            u32 byte_in_word = block_pos_ & 3;

            if (byte_in_word == 0) {
                // Whole words available in this block and message.
                u32 words_avail = (64u - block_pos_) >> 2;
                std::size_t msg_words = (len - i) >> 2;
                u32 words = static_cast<u32>(msg_words < words_avail ? msg_words : words_avail);

                const u32 *ks = block_ + word_idx;
                // Pairs of keystream words XORed as one u64 per iteration.
                u32 w = 0;
                for (; w + 2 <= words; w += 2) {
                    u64 k = static_cast<u64>(ks[w]) | (static_cast<u64>(ks[w + 1]) << 32);
                    u64 m;
                    std::memcpy(&m, in + i, 8);
                    u64 c = m ^ k;
                    std::memcpy(out + i, &c, 8);
                    i += 8;
                    block_pos_ += 8;
                }
                for (; w < words; w++) {
                    u32 m;
                    std::memcpy(&m, in + i, 4);
                    store32_le(out + i, m ^ ks[w]);
                    i += 4;
                    block_pos_ += 4;
                }
            }

            // Byte tail (partial word offset or final sub-word bytes).
            for (; i < len && block_pos_ < 64; i++) {
                u32 wi = block_pos_ >> 2;
                u32 bi = block_pos_ & 3;
                u8 key_byte = static_cast<u8>((block_[wi] >> (bi * 8)) & 0xFF);
                out[i] = in[i] ^ key_byte;
                block_pos_++;
            }
        }
    }

}  // namespace browser::net::crypto
