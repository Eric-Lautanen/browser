#include "bignum.hpp"
#include <algorithm>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

namespace browser::net::crypto {

BigNum::BigNum() : limbs_(1, 0) {}

BigNum::BigNum(u32 value) {
    limbs_.push_back(value);
    normalize();
}

BigNum BigNum::from_bytes(const u8* data, std::size_t len) {
    BigNum r;
    if (len == 0) return r;
    r.limbs_.resize((len + 3) / 4, 0);
    for (std::size_t i = 0; i < len; i++) {
        std::size_t p = len - 1 - i;
        r.limbs_[p / 4] |= static_cast<Limb>(data[i]) << ((p % 4) * 8);
    }
    r.normalize();
    return r;
}

std::vector<u8> BigNum::to_bytes(std::size_t min_size) const {
    if (is_zero()) return std::vector<u8>(min_size > 0 ? min_size : 1, 0);
    std::size_t n = (bit_length() + 7) / 8;
    if (n < min_size) n = min_size;
    std::vector<u8> out(n, 0);
    for (std::size_t i = 0; i < limbs_.size(); i++) {
        Limb v = limbs_[i];
        for (std::size_t j = 0; j < 4 && (i * 4 + j) < n; j++) {
            out[n - 1 - (i * 4 + j)] = static_cast<u8>(v);
            v >>= 8;
        }
    }
    return out;
}

void BigNum::normalize() {
    while (limbs_.size() > 1 && limbs_.back() == 0)
        limbs_.pop_back();
}

bool BigNum::is_zero() const {
    return limbs_.size() == 1 && limbs_[0] == 0;
}

int BigNum::compare(const BigNum& o) const {
    std::size_t n = std::max(limbs_.size(), o.limbs_.size());
    for (std::size_t i = n; i > 0; i--) {
        Limb a = i <= limbs_.size() ? limbs_[i - 1] : 0;
        Limb b = i <= o.limbs_.size() ? o.limbs_[i - 1] : 0;
        if (a != b) return a > b ? 1 : -1;
    }
    return 0;
}

std::size_t BigNum::bit_length() const {
    if (is_zero()) return 0;
    std::size_t i = limbs_.size() - 1;
    Limb v = limbs_[i];
    std::size_t bits = i * LIMB_BITS;
    while (v) { v >>= 1; bits++; }
    return bits;
}

void BigNum::limb_add(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn) {
    u64 c = 0;
    rn = std::max(an, bn);
    for (std::size_t i = 0; i < rn; i++) {
        u64 s = (i < an ? a[i] : 0ULL) + (i < bn ? b[i] : 0ULL) + c;
        r[i] = static_cast<Limb>(s);
        c = s >> 32;
    }
    if (c) { r[rn] = static_cast<Limb>(c); rn++; }
}

void BigNum::limb_sub(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn) {
    i64 br = 0;
    rn = an;
    for (std::size_t i = 0; i < an; i++) {
        i64 d = static_cast<i64>(a[i]) - (i < bn ? static_cast<i64>(b[i]) : 0LL) - br;
        if (d >= 0) { r[i] = static_cast<Limb>(d); br = 0; }
        else { r[i] = static_cast<Limb>(d + (1LL << 32)); br = 1; }
    }
    while (rn > 1 && r[rn - 1] == 0) rn--;
}

void BigNum::limb_mul(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn) {
    rn = an + bn;
    for (std::size_t i = 0; i < rn; i++) r[i] = 0;
    for (std::size_t i = 0; i < an; i++) {
        u64 carry = 0;
        for (std::size_t j = 0; j < bn; j++) {
            u64 p = static_cast<u64>(a[i]) * b[j] + r[i + j] + carry;
            r[i + j] = static_cast<Limb>(p);
            carry = p >> 32;
        }
        r[i + bn] += static_cast<Limb>(carry);
    }
    while (rn > 1 && r[rn - 1] == 0) rn--;
}

int BigNum::limb_cmp(const Limb* a, std::size_t an, const Limb* b, std::size_t bn) {
    if (an != bn) return an > bn ? 1 : -1;
    for (std::size_t i = an; i > 0; i--) {
        if (a[i - 1] != b[i - 1]) return a[i - 1] > b[i - 1] ? 1 : -1;
    }
    return 0;
}

void BigNum::div_mod(const BigNum& dividend, const BigNum& divisor, BigNum& quotient, BigNum& remainder) const {
    if (divisor.is_zero()) { quotient = BigNum(0); remainder = BigNum(0); return; }
    int c = dividend.compare(divisor);
    if (c < 0) { quotient = BigNum(0); remainder = dividend; return; }
    if (c == 0) { quotient = BigNum(1); remainder = BigNum(0); return; }

    remainder = dividend;
    quotient.limbs_.resize(dividend.limbs_.size(), 0);
    std::size_t shift = dividend.bit_length() - divisor.bit_length();

    BigNum sd = divisor;
    {
        std::size_t ws = shift / LIMB_BITS;
        std::size_t bs = shift % LIMB_BITS;
        if (ws > 0) sd.limbs_.insert(sd.limbs_.begin(), ws, 0);
        if (bs > 0) {
            Limb carry = 0;
            for (std::size_t i = 0; i < sd.limbs_.size(); i++) {
                u64 v = (static_cast<u64>(sd.limbs_[i]) << bs) | carry;
                sd.limbs_[i] = static_cast<Limb>(v);
                carry = static_cast<Limb>(v >> 32);
            }
            if (carry) sd.limbs_.push_back(carry);
        }
    }

    for (std::size_t step = 0; step <= shift; step++) {
        std::size_t bp = shift - step;
        if (remainder.compare(sd) >= 0) {
            std::vector<Limb> tmp(remainder.limbs_.size(), 0);
            std::size_t tlen = 0;
            limb_sub(remainder.limbs_.data(), remainder.limbs_.size(),
                     sd.limbs_.data(), sd.limbs_.size(), tmp.data(), tlen);
            remainder.limbs_.assign(tmp.data(), tmp.data() + tlen);
            remainder.normalize();
            std::size_t qw = bp / LIMB_BITS;
            std::size_t qb = bp % LIMB_BITS;
            if (qw >= quotient.limbs_.size())
                quotient.limbs_.resize(qw + 1, 0);
            quotient.limbs_[qw] |= (static_cast<Limb>(1) << qb);
        }
        Limb carry = 0;
        for (std::size_t i = sd.limbs_.size(); i > 0; i--) {
            std::size_t idx = i - 1;
            u64 v = (static_cast<u64>(carry) << 32) | sd.limbs_[idx];
            sd.limbs_[idx] = static_cast<Limb>(v >> 1);
            carry = static_cast<Limb>(v & 1);
        }
        if (!sd.limbs_.empty() && sd.limbs_.back() == 0)
            sd.limbs_.pop_back();
    }
    quotient.normalize();
    remainder.normalize();
}

BigNum BigNum::mod_add(const BigNum& o, const BigNum& m) const {
    std::size_t maxn = std::max(limbs_.size(), o.limbs_.size()) + 1;
    std::vector<Limb> tmp(maxn, 0);
    std::size_t tlen = 0;
    limb_add(limbs_.data(), limbs_.size(), o.limbs_.data(), o.limbs_.size(), tmp.data(), tlen);
    BigNum sum;
    sum.limbs_.assign(tmp.data(), tmp.data() + tlen);
    sum.normalize();
    if (sum.compare(m) >= 0) {
        std::vector<Limb> s2(maxn, 0);
        std::size_t s2len = 0;
        limb_sub(sum.limbs_.data(), sum.limbs_.size(), m.limbs_.data(), m.limbs_.size(), s2.data(), s2len);
        sum.limbs_.assign(s2.data(), s2.data() + s2len);
        sum.normalize();
    }
    return sum;
}

BigNum BigNum::mod_sub(const BigNum& o, const BigNum& m) const {
    if (compare(o) >= 0) {
        std::vector<Limb> tmp(limbs_.size(), 0);
        std::size_t tlen = 0;
        limb_sub(limbs_.data(), limbs_.size(), o.limbs_.data(), o.limbs_.size(), tmp.data(), tlen);
        BigNum r;
        r.limbs_.assign(tmp.data(), tmp.data() + tlen);
        r.normalize();
        return r;
    }
    // a < b: compute (m - (b - a)) which equals a - b + m
    // First compute (b - a) where b >= a
    std::vector<Limb> diff(o.limbs_.size(), 0);
    std::size_t dlen = 0;
    limb_sub(o.limbs_.data(), o.limbs_.size(), limbs_.data(), limbs_.size(), diff.data(), dlen);
    // Then compute (m - (b - a))
    std::vector<Limb> tmp(m.limbs_.size(), 0);
    std::size_t tlen = 0;
    limb_sub(m.limbs_.data(), m.limbs_.size(), diff.data(), dlen, tmp.data(), tlen);
    BigNum r;
    r.limbs_.assign(tmp.data(), tmp.data() + tlen);
    r.normalize();
    return r;
}

BigNum BigNum::mod_mul(const BigNum& o, const BigNum& m) const {
    std::vector<Limb> prod(limbs_.size() + o.limbs_.size(), 0);
    std::size_t plen = 0;
    limb_mul(limbs_.data(), limbs_.size(), o.limbs_.data(), o.limbs_.size(), prod.data(), plen);
    BigNum p;
    p.limbs_.assign(prod.data(), prod.data() + plen);
    p.normalize();
    if (p.compare(m) < 0) return p;
    BigNum q, r;
    div_mod(p, m, q, r);
    return r;
}

BigNum BigNum::mod_exp(const BigNum& e, const BigNum& m) const {
    BigNum r(1);
    BigNum b = *this;
    std::size_t bits = e.bit_length();
    for (std::size_t i = 0; i < bits; i++) {
        if ((e.limbs_[i / LIMB_BITS] >> (i % LIMB_BITS)) & 1)
            r = r.mod_mul(b, m);
        b = b.mod_mul(b, m);
    }
    return r;
}

BigNum BigNum::mod_inverse(const BigNum& modulus) const {
    // Extended Euclidean Algorithm using only integer arithmetic
    BigNum a = *this;
    BigNum m = modulus;

    // Reduce a mod m
    { BigNum q, r; div_mod(a, m, q, r); a = r; }
    if (a.is_zero()) return BigNum(0);

    // Extended Euclidean: find s such that a * s ≡ 1 (mod m)
    // Track remainders: r0 = a, r1 = m, with s0 = coeff of a, s1 = coeff of m
    BigNum r0 = a, r1 = m;
    BigNum s0(1), s1(0);

    while (!r1.is_zero()) {
        BigNum q;
        BigNum new_r;
        div_mod(r0, r1, q, new_r);

        // new_s1 = (s0 - q * s1) mod m  (tracking all values modulo m)
        BigNum q_s1_mod = q.mod_mul(s1, m);
        BigNum new_s1 = s0.mod_sub(q_s1_mod, m);

        r0 = r1;
        r1 = new_r;
        s0 = s1;
        s1 = new_s1;
    }

    if (r0.compare(BigNum(1)) != 0) return BigNum(0);
    // Ensure result in [0, m)
    if (s0.compare(m) >= 0) { BigNum q, r; div_mod(s0, m, q, r); s0 = r; }
    return s0;
}

BigNum BigNum::random(std::size_t bits) {
    if (bits == 0) return BigNum(0);
    std::size_t bytes = (bits + 7) / 8;
    std::vector<u8> buf(bytes, 0);
    NTSTATUS st = BCryptGenRandom(nullptr, buf.data(), static_cast<ULONG>(buf.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st < 0) return BigNum(0);
    std::size_t extra = bytes * 8 - bits;
    if (extra > 0) buf[0] &= static_cast<u8>(0xFF >> extra);
    buf[0] |= static_cast<u8>(1 << ((7 - extra) & 7));
    return from_bytes(buf.data(), buf.size());
}

} // namespace browser::net::crypto
