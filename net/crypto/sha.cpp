#include "sha.hpp"
#include <algorithm>

namespace browser::net::crypto {

// SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes)
static const u32 K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

// SHA-384 constants (first 64 bits of fractional parts of cube roots of first 80 primes)
static const u64 K384[80] = {
    0x428a2f98d728ae22,0x7137449123ef65cd,0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc,
    0x3956c25bf348b538,0x59f111f1b605d019,0x923f82a4af194f9b,0xab1c5ed5da6d8118,
    0xd807aa98a3030242,0x12835b0145706fbe,0x243185be4ee4b28c,0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f,0x80deb1fe3b1696b1,0x9bdc06a725c71235,0xc19bf174cf692694,
    0xe49b69c19ef14ad2,0xefbe4786384f25e3,0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65,
    0x2de92c6f592b0275,0x4a7484aa6ea6e483,0x5cb0a9dcbd41fbd4,0x76f988da831153b5,
    0x983e5152ee66dfab,0xa831c66d2db43210,0xb00327c898fb213f,0xbf597fc7beef0ee4,
    0xc6e00bf33da88fc2,0xd5a79147930aa725,0x06ca6351e003826f,0x142929670a0e6e70,
    0x27b70a8546d22ffc,0x2e1b21385c26c926,0x4d2c6dfc5ac42aed,0x53380d139d95b3df,
    0x650a73548baf63de,0x766a0abb3c77b2a8,0x81c2c92e47edaee6,0x92722c851482353b,
    0xa2bfe8a14cf10364,0xa81a664bbc423001,0xc24b8b70d0f89791,0xc76c51a30654be30,
    0xd192e819d6ef5218,0xd69906245565a910,0xf40e35855771202a,0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8,0x1e376c085141ab53,0x2748774cdf8eeb99,0x34b0bcb5e19b48a8,
    0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb,0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3,
    0x748f82ee5defb2fc,0x78a5636f43172f60,0x84c87814a1f0ab72,0x8cc702081a6439ec,
    0x90befffa23631e28,0xa4506cebde82bde9,0xbef9a3f7b2c67915,0xc67178f2e372532b,
    0xca273eceea26619c,0xd186b8c721c0c207,0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178,
    0x06f067aa72176fba,0x0a637dc5a2c898a6,0x113f9804bef90dae,0x1b710b35131c471b,
    0x28db77f523047d84,0x32caab7b40c72493,0x3c9ebe0a15c9bebc,0x431d67c49c100d4c,
    0x4cc5d4becb3e42b6,0x597f299cfc657e2a,0x5fcb6fab3ad6faec,0x6c44198c4a475817
};

// ---------------------------------------------------------------------------
// SHA-256 helpers
// ---------------------------------------------------------------------------
u32 SHA256::rotr(u32 x, u32 n) { return (x >> n) | (x << (32 - n)); }
u32 SHA256::ch(u32 x, u32 y, u32 z) { return (x & y) ^ (~x & z); }
u32 SHA256::maj(u32 x, u32 y, u32 z) { return (x & y) ^ (x & z) ^ (y & z); }
u32 SHA256::sigma0(u32 x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
u32 SHA256::sigma1(u32 x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
u32 SHA256::big_sigma0(u32 x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
u32 SHA256::big_sigma1(u32 x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }

// ---------------------------------------------------------------------------
// SHA-384 helpers
// ---------------------------------------------------------------------------
u64 SHA384::rotr(u64 x, u32 n) { return (x >> n) | (x << (64 - n)); }
u64 SHA384::ch(u64 x, u64 y, u64 z) { return (x & y) ^ (~x & z); }
u64 SHA384::maj(u64 x, u64 y, u64 z) { return (x & y) ^ (x & z) ^ (y & z); }
u64 SHA384::sigma0(u64 x) { return rotr(x, 1) ^ rotr(x, 8) ^ (x >> 7); }
u64 SHA384::sigma1(u64 x) { return rotr(x, 19) ^ rotr(x, 61) ^ (x >> 6); }
u64 SHA384::big_sigma0(u64 x) { return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39); }
u64 SHA384::big_sigma1(u64 x) { return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41); }

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------
SHA256::SHA256() { reset(); }

void SHA256::reset() {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    std::memset(buffer_, 0, 64);
    buffer_len_ = 0;
    count_ = 0;
}

void SHA256::update(const u8* data, std::size_t len) {
    if (len == 0) return;
    count_ += len;
    std::size_t space = 64 - buffer_len_;
    if (len >= space) {
        std::memcpy(buffer_ + buffer_len_, data, space);
        transform(buffer_);
        data += space;
        len -= space;
        while (len >= 64) {
            transform(data);
            data += 64;
            len -= 64;
        }
        buffer_len_ = 0;
    }
    if (len > 0) {
        std::memcpy(buffer_ + buffer_len_, data, len);
        buffer_len_ += len;
    }
}

void SHA256::transform(const u8 block[64]) {
    u32 w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<u32>(block[i * 4])     << 24) |
               (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8)  |
               (static_cast<u32>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; i++) {
        w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
    }

    u32 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u32 e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; i++) {
        u32 S1 = big_sigma1(e);
        u32 ch_val = ch(e, f, g);
        u32 temp1 = h + S1 + ch_val + K256[i] + w[i];
        u32 S0 = big_sigma0(a);
        u32 maj_val = maj(a, b, c);
        u32 temp2 = S0 + maj_val;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::vector<u8> SHA256::digest() {
    u64 bit_count = count_ << 3;
    std::size_t pad_len = (64 - (buffer_len_ + 8) % 64) % 64;
    if (pad_len == 0) pad_len = 64;

    u8 pad[72];
    std::size_t idx = 0;
    pad[idx++] = 0x80;
    while (idx < pad_len) pad[idx++] = 0;
    pad[idx++] = static_cast<u8>(bit_count >> 56);
    pad[idx++] = static_cast<u8>(bit_count >> 48);
    pad[idx++] = static_cast<u8>(bit_count >> 40);
    pad[idx++] = static_cast<u8>(bit_count >> 32);
    pad[idx++] = static_cast<u8>(bit_count >> 24);
    pad[idx++] = static_cast<u8>(bit_count >> 16);
    pad[idx++] = static_cast<u8>(bit_count >> 8);
    pad[idx++] = static_cast<u8>(bit_count);

    update(pad, idx);

    std::vector<u8> out(32);
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = static_cast<u8>(state_[i] >> 24);
        out[i * 4 + 1] = static_cast<u8>(state_[i] >> 16);
        out[i * 4 + 2] = static_cast<u8>(state_[i] >> 8);
        out[i * 4 + 3] = static_cast<u8>(state_[i]);
    }

    reset();
    return out;
}

std::vector<u8> SHA256::hash(const u8* data, std::size_t len) {
    SHA256 h;
    h.update(data, len);
    return h.digest();
}

// ---------------------------------------------------------------------------
// SHA-384
// ---------------------------------------------------------------------------
SHA384::SHA384() { reset(); }

void SHA384::reset() {
    state_[0] = 0xcbbb9d5dc1059ed8;
    state_[1] = 0x629a292a367cd507;
    state_[2] = 0x9159015a3070dd17;
    state_[3] = 0x152fecd8f70e5939;
    state_[4] = 0x67332667ffc00b31;
    state_[5] = 0x8eb44a8768581511;
    state_[6] = 0xdb0c2e0d64f98fa7;
    state_[7] = 0x47b5481dbefa4fa4;
    buffer_len_ = 0;
    count_lo_ = 0;
    count_hi_ = 0;
}

void SHA384::update(const u8* data, std::size_t len) {
    if (len == 0) return;
    count_lo_ += len;
    if (count_lo_ < len) count_hi_++;

    std::size_t space = 128 - buffer_len_;
    if (len >= space) {
        std::memcpy(buffer_ + buffer_len_, data, space);
        transform(buffer_);
        data += space;
        len -= space;
        while (len >= 128) {
            transform(data);
            data += 128;
            len -= 128;
        }
        buffer_len_ = 0;
    }
    if (len > 0) {
        std::memcpy(buffer_ + buffer_len_, data, len);
        buffer_len_ += len;
    }
}

void SHA384::transform(const u8 block[128]) {
    u64 w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<u64>(block[i * 8])     << 56) |
               (static_cast<u64>(block[i * 8 + 1]) << 48) |
               (static_cast<u64>(block[i * 8 + 2]) << 40) |
               (static_cast<u64>(block[i * 8 + 3]) << 32) |
               (static_cast<u64>(block[i * 8 + 4]) << 24) |
               (static_cast<u64>(block[i * 8 + 5]) << 16) |
               (static_cast<u64>(block[i * 8 + 6]) << 8)  |
               (static_cast<u64>(block[i * 8 + 7]));
    }
    for (int i = 16; i < 80; i++) {
        w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
    }

    u64 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u64 e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 80; i++) {
        u64 S1 = big_sigma1(e);
        u64 ch_val = ch(e, f, g);
        u64 temp1 = h + S1 + ch_val + K384[i] + w[i];
        u64 S0 = big_sigma0(a);
        u64 maj_val = maj(a, b, c);
        u64 temp2 = S0 + maj_val;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::vector<u8> SHA384::digest() {
    u64 bit_count_hi = (count_hi_ << 3) | (count_lo_ >> 61);
    u64 bit_count_lo = count_lo_ << 3;

    std::size_t pad_len = (128 - (buffer_len_ + 16) % 128) % 128;
    if (pad_len == 0) pad_len = 128;

    u8 pad[144];
    std::size_t idx = 0;
    pad[idx++] = 0x80;
    while (idx < pad_len) pad[idx++] = 0;
    // 128-bit big-endian bit count
    pad[idx++] = static_cast<u8>(bit_count_hi >> 56);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 48);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 40);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 32);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 24);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 16);
    pad[idx++] = static_cast<u8>(bit_count_hi >> 8);
    pad[idx++] = static_cast<u8>(bit_count_hi);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 56);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 48);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 40);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 32);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 24);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 16);
    pad[idx++] = static_cast<u8>(bit_count_lo >> 8);
    pad[idx++] = static_cast<u8>(bit_count_lo);

    update(pad, idx);

    std::vector<u8> out(48);
    for (int i = 0; i < 6; i++) {
        out[i * 8]     = static_cast<u8>(state_[i] >> 56);
        out[i * 8 + 1] = static_cast<u8>(state_[i] >> 48);
        out[i * 8 + 2] = static_cast<u8>(state_[i] >> 40);
        out[i * 8 + 3] = static_cast<u8>(state_[i] >> 32);
        out[i * 8 + 4] = static_cast<u8>(state_[i] >> 24);
        out[i * 8 + 5] = static_cast<u8>(state_[i] >> 16);
        out[i * 8 + 6] = static_cast<u8>(state_[i] >> 8);
        out[i * 8 + 7] = static_cast<u8>(state_[i]);
    }

    reset();
    return out;
}

std::vector<u8> SHA384::hash(const u8* data, std::size_t len) {
    SHA384 h;
    h.update(data, len);
    return h.digest();
}

// ---------------------------------------------------------------------------
// HMAC-SHA256  (RFC 2104)
// ---------------------------------------------------------------------------
std::vector<u8> hmac_sha256(const std::vector<u8>& key, const std::vector<u8>& data) {
    std::vector<u8> k(key);
    if (k.size() > 64) {
        k = SHA256::hash(k.data(), k.size());
    }
    if (k.size() < 64) {
        k.resize(64, 0);
    }

    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = static_cast<u8>(k[i] ^ 0x36);
        opad[i] = static_cast<u8>(k[i] ^ 0x5c);
    }

    std::vector<u8> inner_input(ipad, ipad + 64);
    inner_input.insert(inner_input.end(), data.begin(), data.end());
    std::vector<u8> inner_hash = SHA256::hash(inner_input.data(), inner_input.size());

    std::vector<u8> outer_input(opad, opad + 64);
    outer_input.insert(outer_input.end(), inner_hash.begin(), inner_hash.end());
    return SHA256::hash(outer_input.data(), outer_input.size());
}

// ---------------------------------------------------------------------------
// HKDF  (RFC 5869)
// ---------------------------------------------------------------------------
std::vector<u8> HKDF::extract(const std::vector<u8>& salt, const std::vector<u8>& ikm) {
    auto s = salt;
    if (s.empty()) {
        s.assign(32, 0);
    }
    return hmac_sha256(s, ikm);
}

std::vector<u8> HKDF::expand(const std::vector<u8>& prk, const std::vector<u8>& info, std::size_t length) {
    std::vector<u8> output;
    output.reserve(length);
    std::vector<u8> t_prev;
    u8 counter = 1;

    while (output.size() < length) {
        std::vector<u8> input;
        input.insert(input.end(), t_prev.begin(), t_prev.end());
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter);
        counter++;

        std::vector<u8> t = hmac_sha256(prk, input);
        std::size_t needed = length - output.size();
        std::size_t take = (needed < t.size()) ? needed : t.size();
        output.insert(output.end(), t.begin(), t.begin() + static_cast<std::ptrdiff_t>(take));
        t_prev = std::move(t);
    }

    return output;
}

std::vector<u8> HKDF::derive(const std::vector<u8>& salt, const std::vector<u8>& ikm,
                               const std::vector<u8>& info, std::size_t length) {
    std::vector<u8> prk = extract(salt, ikm);
    return expand(prk, info, length);
}

} // namespace browser::net::crypto
