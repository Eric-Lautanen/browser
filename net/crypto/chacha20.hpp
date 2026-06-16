#pragma once
#include "../../tests/utility.hpp"
#include <cstring>
#include <vector>

namespace browser::net::crypto {

class ChaCha20 {
public:
    ChaCha20();
    void set_key(const u8 key[32]);
    void set_nonce(const u8 nonce[12]);
    void set_counter(u32 counter);
    void encrypt(const u8* in, std::size_t len, u8* out);

private:
    u32 state_[16];
    u32 block_[16];
    bool has_block_ = false;
    u8 block_pos_ = 0;
    u8 key_[32];
    u8 nonce_[12];
    u32 counter_ = 0;

    static u32 rotl(u32 x, int n);
    static void quarter_round(u32& a, u32& b, u32& c, u32& d);
    void next_block();
};

} // namespace browser::net::crypto
