#pragma once
#include "../../tests/utility.hpp"
#include <vector>
#include <cstring>

namespace browser::net::crypto {

class SHA256 {
public:
    SHA256();
    SHA256(const SHA256&) = default;
    SHA256& operator=(const SHA256&) = default;
    void update(const u8* data, std::size_t len);
    std::vector<u8> digest();
    void reset();
    static std::vector<u8> hash(const u8* data, std::size_t len);
private:
    u32 state_[8];
    u8 buffer_[64];
    std::size_t buffer_len_;
    u64 count_;
    void transform(const u8 block[64]);
    static u32 rotr(u32 x, u32 n);
    static u32 ch(u32 x, u32 y, u32 z);
    static u32 maj(u32 x, u32 y, u32 z);
    static u32 sigma0(u32 x);
    static u32 sigma1(u32 x);
    static u32 big_sigma0(u32 x);
    static u32 big_sigma1(u32 x);
};

class SHA384 {
public:
    SHA384();
    void update(const u8* data, std::size_t len);
    std::vector<u8> digest();
    void reset();
    static std::vector<u8> hash(const u8* data, std::size_t len);
private:
    u64 state_[8];
    u8 buffer_[128];
    std::size_t buffer_len_;
    u64 count_lo_, count_hi_;
    void transform(const u8 block[128]);
    static u64 rotr(u64 x, u32 n);
    static u64 ch(u64 x, u64 y, u64 z);
    static u64 maj(u64 x, u64 y, u64 z);
    static u64 sigma0(u64 x);
    static u64 sigma1(u64 x);
    static u64 big_sigma0(u64 x);
    static u64 big_sigma1(u64 x);
};

std::vector<u8> hmac_sha256(const std::vector<u8>& key, const std::vector<u8>& data);

class HKDF {
public:
    static std::vector<u8> extract(const std::vector<u8>& salt, const std::vector<u8>& ikm);
    static std::vector<u8> expand(const std::vector<u8>& prk, const std::vector<u8>& info, std::size_t length);
    static std::vector<u8> derive(const std::vector<u8>& salt, const std::vector<u8>& ikm,
                                   const std::vector<u8>& info, std::size_t length);
};

} // namespace browser::net::crypto
