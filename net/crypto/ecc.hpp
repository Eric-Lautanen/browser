#pragma once
#include "bignum.hpp"

namespace browser::net::crypto {

struct ECPoint {
    BigNum x, y;
    bool is_infinity = false;
};

class EllipticCurve {
public:
    const BigNum p;
    const BigNum a;
    const BigNum b;
    const BigNum order;
    const ECPoint generator;

    /* SECURITY WARNING: Not constant-time. Timing side-channels can extract private keys.
       Acceptable for learning project only. */

    EllipticCurve(const BigNum& p_, const BigNum& a_, const BigNum& b_,
                  const BigNum& order_, const ECPoint& gen_);

    ECPoint point_add(const ECPoint& p1, const ECPoint& p2) const;
    ECPoint point_mul(const ECPoint& point, const BigNum& scalar) const;
    ECPoint point_neg(const ECPoint& point) const;
    bool is_on_curve(const ECPoint& point) const;

    static EllipticCurve secp256r1();
    static EllipticCurve secp384r1();

private:
    ECPoint double_point(const ECPoint& p) const;
};

} // namespace browser::net::crypto
