#pragma once
#include "../../core/utility.hpp"

#include <cstring>
#include <vector>

namespace browser::net::crypto {

    class Poly1305 {
    public:
        Poly1305();
        void set_key(const u8 key[32]);
        void update(const u8 *data, std::size_t len);
        void finish(u8 mac[16]);

    private:
        // r is stored as five 26-bit limbs (clamped r reaches bit ~123, so four
        // limbs cannot hold it); s is the 128-bit pad in four 32-bit words.
        u32 r_[5];
        u32 s_[4];
        u32 h_[5];
        u8 buf_[16];
        std::size_t buf_len_ = 0;
        static u32 load32_le(const u8 *p);
        // N-P2: processes any aligned-or-not 16-byte block straight from the
        // caller's buffer; hibit is 1<<24 for full blocks, 0 for the tail.
        void process_block(const u8 *p, u32 hibit);
    };

}  // namespace browser::net::crypto
