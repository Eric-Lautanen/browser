#include "x25519.hpp"
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

namespace browser::net::crypto {

/* SECURITY WARNING: Not constant-time. Timing side-channels can extract private keys.
   Acceptable for learning project only. */

/* Adapted from curve25519-donna-c64 by Adam Langley (public domain) */

using limb = u64;
using felem = limb[5];
static constexpr limb MASK51 = 0x7FFFFFFFFFFFFULL;

static limb load64_le(const u8* p) {
    return static_cast<limb>(p[0]) | (static_cast<limb>(p[1]) << 8) |
           (static_cast<limb>(p[2]) << 16) | (static_cast<limb>(p[3]) << 24) |
           (static_cast<limb>(p[4]) << 32) | (static_cast<limb>(p[5]) << 40) |
           (static_cast<limb>(p[6]) << 48) | (static_cast<limb>(p[7]) << 56);
}

static void store64_le(u8* p, limb v) {
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
    p[4] = static_cast<u8>(v >> 32);
    p[5] = static_cast<u8>(v >> 40);
    p[6] = static_cast<u8>(v >> 48);
    p[7] = static_cast<u8>(v >> 56);
}

static void fexpand(limb* output, const u8* in) {
    output[0] = load64_le(in) & MASK51;
    output[1] = (load64_le(in + 6) >> 3) & MASK51;
    output[2] = (load64_le(in + 12) >> 6) & MASK51;
    output[3] = (load64_le(in + 19) >> 1) & MASK51;
    output[4] = (load64_le(in + 24) >> 12) & MASK51;
}

static void fcontract(u8* output, const felem input) {
    uint64_t t[5];
    for (int i = 0; i < 5; i++) t[i] = input[i];

    for (int pass = 0; pass < 2; pass++) {
        t[1] += t[0] >> 51; t[0] &= MASK51;
        t[2] += t[1] >> 51; t[1] &= MASK51;
        t[3] += t[2] >> 51; t[2] &= MASK51;
        t[4] += t[3] >> 51; t[3] &= MASK51;
        t[0] += 19 * (t[4] >> 51); t[4] &= MASK51;
    }

    t[0] += 19;
    t[1] += t[0] >> 51; t[0] &= MASK51;
    t[2] += t[1] >> 51; t[1] &= MASK51;
    t[3] += t[2] >> 51; t[2] &= MASK51;
    t[4] += t[3] >> 51; t[3] &= MASK51;
    t[0] += 19 * (t[4] >> 51); t[4] &= MASK51;

    t[0] += 0x8000000000000ULL - 19;
    t[1] += 0x8000000000000ULL - 1;
    t[2] += 0x8000000000000ULL - 1;
    t[3] += 0x8000000000000ULL - 1;
    t[4] += 0x8000000000000ULL - 1;

    t[1] += t[0] >> 51; t[0] &= MASK51;
    t[2] += t[1] >> 51; t[1] &= MASK51;
    t[3] += t[2] >> 51; t[2] &= MASK51;
    t[4] += t[3] >> 51; t[3] &= MASK51;
    t[4] &= MASK51;

    store64_le(output,    t[0] | (t[1] << 51));
    store64_le(output+8,  (t[1] >> 13) | (t[2] << 38));
    store64_le(output+16, (t[2] >> 26) | (t[3] << 25));
    store64_le(output+24, (t[3] >> 39) | (t[4] << 12));
}

static void fsum(limb* output, const limb* in) {
    output[0] += in[0];
    output[1] += in[1];
    output[2] += in[2];
    output[3] += in[3];
    output[4] += in[4];
}

static void fdifference_backwards(limb* out, const limb* in) {
    static const limb two54m152 = (1ULL << 54) - 152;
    static const limb two54m8 = (1ULL << 54) - 8;
    out[0] = in[0] + two54m152 - out[0];
    out[1] = in[1] + two54m8 - out[1];
    out[2] = in[2] + two54m8 - out[2];
    out[3] = in[3] + two54m8 - out[3];
    out[4] = in[4] + two54m8 - out[4];
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

static void fscalar_product(limb* output, const limb* in, limb scalar) {
    unsigned __int128 a;
    a = (unsigned __int128)in[0] * scalar;
    output[0] = (limb)a & MASK51;
    a = (unsigned __int128)in[1] * scalar + (limb)(a >> 51);
    output[1] = (limb)a & MASK51;
    a = (unsigned __int128)in[2] * scalar + (limb)(a >> 51);
    output[2] = (limb)a & MASK51;
    a = (unsigned __int128)in[3] * scalar + (limb)(a >> 51);
    output[3] = (limb)a & MASK51;
    a = (unsigned __int128)in[4] * scalar + (limb)(a >> 51);
    output[4] = (limb)a & MASK51;
    output[0] += (limb)(a >> 51) * 19;
}

static void fmul(limb* output, const limb* in2, const limb* in) {
    limb r0 = in[0], r1 = in[1], r2 = in[2], r3 = in[3], r4 = in[4];
    limb s0 = in2[0], s1 = in2[1], s2 = in2[2], s3 = in2[3], s4 = in2[4];

    unsigned __int128 t0 = (unsigned __int128)r0 * s0;
    unsigned __int128 t1 = (unsigned __int128)r0 * s1 + (unsigned __int128)r1 * s0;
    unsigned __int128 t2 = (unsigned __int128)r0 * s2 + (unsigned __int128)r2 * s0 + (unsigned __int128)r1 * s1;
    unsigned __int128 t3 = (unsigned __int128)r0 * s3 + (unsigned __int128)r3 * s0 + (unsigned __int128)r1 * s2 + (unsigned __int128)r2 * s1;
    unsigned __int128 t4 = (unsigned __int128)r0 * s4 + (unsigned __int128)r4 * s0 + (unsigned __int128)r3 * s1 + (unsigned __int128)r1 * s3 + (unsigned __int128)r2 * s2;

    r4 *= 19; r1 *= 19; r2 *= 19; r3 *= 19;

    t0 += (unsigned __int128)r4 * s1 + (unsigned __int128)r1 * s4 + (unsigned __int128)r2 * s3 + (unsigned __int128)r3 * s2;
    t1 += (unsigned __int128)r4 * s2 + (unsigned __int128)r2 * s4 + (unsigned __int128)r3 * s3;
    t2 += (unsigned __int128)r4 * s3 + (unsigned __int128)r3 * s4;
    t3 += (unsigned __int128)r4 * s4;

    limb carry;
    r0 = (limb)t0 & MASK51; carry = (limb)(t0 >> 51); t1 += carry;
    r1 = (limb)t1 & MASK51; carry = (limb)(t1 >> 51); t2 += carry;
    r2 = (limb)t2 & MASK51; carry = (limb)(t2 >> 51); t3 += carry;
    r3 = (limb)t3 & MASK51; carry = (limb)(t3 >> 51); t4 += carry;
    r4 = (limb)t4 & MASK51; carry = (limb)(t4 >> 51);
    r0 += carry * 19; carry = r0 >> 51; r0 &= MASK51;
    r1 += carry;       carry = r1 >> 51; r1 &= MASK51;
    r2 += carry;

    output[0] = r0; output[1] = r1; output[2] = r2; output[3] = r3; output[4] = r4;
}

static void fsquare_times(limb* output, const limb* in, limb count) {
    limb r0 = in[0], r1 = in[1], r2 = in[2], r3 = in[3], r4 = in[4];

    do {
        limb d0 = r0 * 2;
        limb d1 = r1 * 2;
        limb d2 = r2 * 2 * 19;
        limb d419 = r4 * 19;
        limb d4 = d419 * 2;

        unsigned __int128 t0 = (unsigned __int128)r0 * r0 + (unsigned __int128)d4 * r1 + (unsigned __int128)d2 * r3;
        unsigned __int128 t1 = (unsigned __int128)d0 * r1 + (unsigned __int128)d4 * r2 + (unsigned __int128)r3 * (r3 * 19);
        unsigned __int128 t2 = (unsigned __int128)d0 * r2 + (unsigned __int128)r1 * r1 + (unsigned __int128)d4 * r3;
        unsigned __int128 t3 = (unsigned __int128)d0 * r3 + (unsigned __int128)d1 * r2 + (unsigned __int128)r4 * d419;
        unsigned __int128 t4 = (unsigned __int128)d0 * r4 + (unsigned __int128)d1 * r3 + (unsigned __int128)r2 * r2;

        limb carry;
        r0 = (limb)t0 & MASK51; carry = (limb)(t0 >> 51); t1 += carry;
        r1 = (limb)t1 & MASK51; carry = (limb)(t1 >> 51); t2 += carry;
        r2 = (limb)t2 & MASK51; carry = (limb)(t2 >> 51); t3 += carry;
        r3 = (limb)t3 & MASK51; carry = (limb)(t3 >> 51); t4 += carry;
        r4 = (limb)t4 & MASK51; carry = (limb)(t4 >> 51);
        r0 += carry * 19; carry = r0 >> 51; r0 &= MASK51;
        r1 += carry;       carry = r1 >> 51; r1 &= MASK51;
        r2 += carry;
    } while (--count);

    output[0] = r0; output[1] = r1; output[2] = r2; output[3] = r3; output[4] = r4;
}

#pragma GCC diagnostic pop

static void fmonty(limb *x2, limb *z2, limb *x3, limb *z3,
                   limb *x, limb *z, limb *xprime, limb *zprime,
                   const limb *qmqp) {
    limb origx[5], origxprime[5], zzz[5], xx[5], zz[5], xxprime[5], zzprime[5], zzzprime[5];

    memcpy(origx, x, 5 * sizeof(limb));
    fsum(x, z);
    fdifference_backwards(z, origx);

    memcpy(origxprime, xprime, sizeof(limb) * 5);
    fsum(xprime, zprime);
    fdifference_backwards(zprime, origxprime);

    fmul(xxprime, xprime, z);
    fmul(zzprime, x, zprime);

    memcpy(origxprime, xxprime, sizeof(limb) * 5);
    fsum(xxprime, zzprime);
    fdifference_backwards(zzprime, origxprime);

    fsquare_times(x3, xxprime, 1);
    fsquare_times(zzzprime, zzprime, 1);
    fmul(z3, zzzprime, qmqp);

    fsquare_times(xx, x, 1);
    fsquare_times(zz, z, 1);
    fmul(x2, xx, zz);

    fdifference_backwards(zz, xx);
    fscalar_product(zzz, zz, 121665);
    fsum(zzz, xx);
    fmul(z2, zz, zzz);
}

static void swap_conditional(limb a[5], limb b[5], limb iswap) {
    limb swap = -iswap;
    for (unsigned i = 0; i < 5; ++i) {
        limb x = swap & (a[i] ^ b[i]);
        a[i] ^= x;
        b[i] ^= x;
    }
}

static void cmult(limb *resultx, limb *resultz, const u8 *n, const limb *q) {
    limb a[5] = {0}, b[5] = {1}, c[5] = {1}, d[5] = {0};
    limb *nqpqx = a, *nqpqz = b, *nqx = c, *nqz = d, *t;
    limb e[5] = {0}, f[5] = {1}, g[5] = {0}, h[5] = {1};
    limb *nqpqx2 = e, *nqpqz2 = f, *nqx2 = g, *nqz2 = h;

    memcpy(nqpqx, q, sizeof(limb) * 5);

    for (unsigned i = 0; i < 32; ++i) {
        u8 byte = n[31 - i];
        for (unsigned j = 0; j < 8; ++j) {
            limb bit = byte >> 7;

            swap_conditional(nqx, nqpqx, bit);
            swap_conditional(nqz, nqpqz, bit);
            fmonty(nqx2, nqz2, nqpqx2, nqpqz2, nqx, nqz, nqpqx, nqpqz, q);
            swap_conditional(nqx2, nqpqx2, bit);
            swap_conditional(nqz2, nqpqz2, bit);

            t = nqx; nqx = nqx2; nqx2 = t;
            t = nqz; nqz = nqz2; nqz2 = t;
            t = nqpqx; nqpqx = nqpqx2; nqpqx2 = t;
            t = nqpqz; nqpqz = nqpqz2; nqpqz2 = t;

            byte <<= 1;
        }
    }

    memcpy(resultx, nqx, sizeof(limb) * 5);
    memcpy(resultz, nqz, sizeof(limb) * 5);
}

static void crecip(limb* out, const limb* z) {
    felem a, t0, b, c;
    /* 2 */ fsquare_times(a, z, 1);
    /* 8 */ fsquare_times(t0, a, 2);
    /* 9 */ fmul(b, t0, z);
    /* 11 */ fmul(a, b, a);
    /* 22 */ fsquare_times(t0, a, 1);
    /* 2^5 - 2^0 = 31 */ fmul(b, t0, b);
    /* 2^10 - 2^5 */ fsquare_times(t0, b, 5);
    /* 2^10 - 2^0 */ fmul(b, t0, b);
    /* 2^20 - 2^10 */ fsquare_times(t0, b, 10);
    /* 2^20 - 2^0 */ fmul(c, t0, b);
    /* 2^40 - 2^20 */ fsquare_times(t0, c, 20);
    /* 2^40 - 2^0 */ fmul(t0, t0, c);
    /* 2^50 - 2^10 */ fsquare_times(t0, t0, 10);
    /* 2^50 - 2^0 */ fmul(b, t0, b);
    /* 2^100 - 2^50 */ fsquare_times(t0, b, 50);
    /* 2^100 - 2^0 */ fmul(c, t0, b);
    /* 2^200 - 2^100 */ fsquare_times(t0, c, 100);
    /* 2^200 - 2^0 */ fmul(t0, t0, c);
    /* 2^250 - 2^50 */ fsquare_times(t0, t0, 50);
    /* 2^250 - 2^0 */ fmul(t0, t0, b);
    /* 2^255 - 2^5 */ fsquare_times(t0, t0, 5);
    /* 2^255 - 21 */ fmul(out, t0, a);
}

void X25519::clamp(u8 key[32]) {
    key[0] &= 248;
    key[31] &= 127;
    key[31] |= 64;
}

void X25519::generate_keypair(u8 priv[32], u8 pub[32]) {
    BCryptGenRandom(nullptr, priv, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    clamp(priv);
    u8 base[32] = {9};
    mul(pub, priv, base);
}

void X25519::shared_secret(const u8 priv[32], const u8 peer_pub[32], u8 out[32]) {
    u8 p[32];
    std::memcpy(p, peer_pub, 32);
    mul(out, priv, p);
}

void X25519::mul(u8 result[32], const u8 scalar[32], const u8 point[32]) {
    u8 e[32];
    std::memcpy(e, scalar, 32);
    clamp(e);

    limb bp[5], x[5], z[5], zmone[5];
    fexpand(bp, point);
    cmult(x, z, e, bp);
    crecip(zmone, z);
    fmul(z, x, zmone);
    fcontract(result, z);
}

} // namespace browser::net::crypto
