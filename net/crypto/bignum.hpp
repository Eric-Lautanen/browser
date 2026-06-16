#pragma once
#include "../../tests/utility.hpp"
#include <vector>
#include <cstring>

namespace browser::net::crypto {

class BigNum {
public:
    using Limb = u32;
    static constexpr std::size_t LIMB_BITS = 32;

    BigNum();
    explicit BigNum(u32 value);
    BigNum(const BigNum&) = default;
    BigNum& operator=(const BigNum&) = default;

    static BigNum from_bytes(const u8* data, std::size_t len);
    std::vector<u8> to_bytes(std::size_t min_size = 0) const;

    BigNum mod_add(const BigNum& other, const BigNum& modulus) const;
    BigNum mod_sub(const BigNum& other, const BigNum& modulus) const;
    BigNum mod_mul(const BigNum& other, const BigNum& modulus) const;
    BigNum mod_exp(const BigNum& exponent, const BigNum& modulus) const;
    BigNum mod_inverse(const BigNum& modulus) const;

    int compare(const BigNum& other) const;
    bool is_zero() const;
    std::size_t bit_length() const;

    static BigNum random(std::size_t bits);

    void normalize();
    const std::vector<Limb>& limbs() const { return limbs_; }

private:
    std::vector<Limb> limbs_;

    static void limb_add(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn);
    static void limb_sub(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn);
    static void limb_mul(const Limb* a, std::size_t an, const Limb* b, std::size_t bn, Limb* r, std::size_t& rn);
    static int limb_cmp(const Limb* a, std::size_t an, const Limb* b, std::size_t bn);

    void div_mod(const BigNum& dividend, const BigNum& divisor, BigNum& quotient, BigNum& remainder) const;
};

} // namespace browser::net::crypto
