#pragma once
#include "../../tests/utility.hpp"
#include <vector>
#include <cstring>

namespace browser::net::crypto {

class AES {
public:
    AES();
    void set_key(const u8* key, std::size_t key_len);
    void set_iv(const u8* iv, std::size_t iv_len);

    std::vector<u8> encrypt_gcm(const u8* data, std::size_t len,
                                 const u8* aad, std::size_t aad_len,
                                 u8 tag[16]);
    bool decrypt_gcm(const u8* data, std::size_t len,
                     const u8* aad, std::size_t aad_len,
                     const u8 tag[16]);
    std::vector<u8> decrypt_gcm_return(const u8* data, std::size_t len,
                                        const u8* aad, std::size_t aad_len,
                                        const u8 tag[16]);

private:
    static constexpr std::size_t BLOCK_SIZE = 16;
    u8 key_[32];
    std::size_t key_len_ = 0;
    u8 iv_[12];
    std::size_t iv_len_ = 0;

    u32 rk_[60];
    std::size_t nk_;
    std::size_t nr_;

    void key_expansion();
    void encrypt_block(const u8 in[16], u8 out[16]) const;

    void gcm_incr(u8* x) const;
    void gcm_ghash(const u8* aad, std::size_t aad_len,
                   const u8* ciphertext, std::size_t ct_len,
                   u8 out[16]) const;
    void gcm_gctr(const u8* icb, const u8* in, std::size_t in_len, u8* out) const;
    void gf128_mul(const u8 x[16], const u8 y[16], u8 z[16]) const;

    static u32 xtime(u32 x);
    static void xor_block(const u8 a[16], const u8 b[16], u8 out[16]);
    static u32 load_word(const u8* p);
    static void store_word(u8* p, u32 w);
    static u32 sub_word(u32 w);
    static u32 rot_word(u32 w);
};

} // namespace browser::net::crypto
