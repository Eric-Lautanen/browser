#pragma once
#include "../../tests/utility.hpp"
#include <cstring>
#include <vector>

namespace browser::net::crypto {

class Poly1305 {
public:
    Poly1305();
    void set_key(const u8 key[32]);
    void update(const u8* data, std::size_t len);
    void finish(u8 mac[16]);

private:
    u32 r_[4];
    u32 s_[4];
    u32 h_[5];
    u8 buf_[16];
    std::size_t buf_len_ = 0;
    static u32 load32_le(const u8* p);
    void process_block();
};

} // namespace browser::net::crypto
