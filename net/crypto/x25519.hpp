#pragma once
#include "../../tests/utility.hpp"

namespace browser::net::crypto {

class X25519 {
public:
    static void generate_keypair(u8 priv[32], u8 pub[32]);
    static void shared_secret(const u8 priv[32], const u8 peer_pub[32], u8 out[32]);

private:
    /* SECURITY WARNING: Not constant-time. Timing side-channels can extract private keys. */
    static void clamp(u8 key[32]);
    static void mul(u8 result[32], const u8 scalar[32], const u8 point[32]);
};

} // namespace browser::net::crypto
