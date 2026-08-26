#include "../net/tls.hpp"

#include "../net/crypto/aes.hpp"
#include "../net/crypto/bignum.hpp"
#include "../net/crypto/chacha20.hpp"
#include "../net/crypto/ecc.hpp"
#include "../net/crypto/poly1305.hpp"
#include "../net/crypto/sha.hpp"
#include "../net/crypto/x25519.hpp"
#include "../net/tls/cert_verify.hpp"
#include "test_framework.hpp"
#include "utility.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace browser;
using namespace browser::net::crypto;
using namespace browser::net::tls;

// BigNum tests

TEST(bignum_add, {
    BigNum a(5);
    BigNum b(7);
    BigNum mod(13);
    auto r = a.mod_add(b, mod);
    ASSERT_EQ(r.compare(BigNum(12)), 0);
})

TEST(bignum_sub, {
    BigNum a(10);
    BigNum b(3);
    BigNum mod(13);
    auto r = a.mod_sub(b, mod);
    ASSERT_EQ(r.compare(BigNum(7)), 0);
})

TEST(bignum_mod_exp, {
    auto r = BigNum(2).mod_exp(BigNum(10), BigNum(1000));
    ASSERT_EQ(r.compare(BigNum(24)), 0);
})

TEST(bignum_inverse, {
    auto inv = BigNum(3).mod_inverse(BigNum(7));
    ASSERT_EQ(inv.compare(BigNum(5)), 0);
})

TEST(bignum_bytes, {
    BigNum n;
    n = BigNum::from_bytes((const u8*)"\x01\x02", 2);
    auto b = n.to_bytes();
    ASSERT_EQ(b.size(), 2u);
    ASSERT_EQ(b[0], 1);
    ASSERT_EQ(b[1], 2);
})

TEST(bignum_random, {
    auto r = BigNum::random(128);
    ASSERT(r.bit_length() == 128);
})

// ECC tests

TEST(ecc_mul_gen, {
    auto c = EllipticCurve::secp256r1();
    auto r = c.point_mul(c.generator, BigNum(1));
    ASSERT(r.x.compare(c.generator.x) == 0);
})

TEST(ecc_double_vs_add, {
    auto c = EllipticCurve::secp256r1();
    auto d = c.point_mul(c.generator, BigNum(2));
    auto a = c.point_add(c.generator, c.generator);
    ASSERT(d.x.compare(a.x) == 0);
})

TEST(ecc_on_curve, {
    auto c = EllipticCurve::secp256r1();
    ASSERT(c.is_on_curve(c.generator));
})

TEST(ecc_neg_add, {
    auto c = EllipticCurve::secp256r1();
    auto neg = c.point_neg(c.generator);
    auto sum = c.point_add(c.generator, neg);
    ASSERT(sum.is_infinity);
})

TEST(ecc_secp384r1_mul1, {
    auto c = EllipticCurve::secp384r1();
    BigNum one(1);
    auto r = one.mod_mul(one, c.p);
    ASSERT_EQ(one.compare(r), 0);
})
TEST(ecc_secp384r1_gen, {
    auto c = EllipticCurve::secp384r1();
    BigNum lhs = c.generator.y.mod_mul(c.generator.y, c.p);
    BigNum t = c.generator.x.mod_mul(c.generator.x, c.p);
    BigNum x3 = t.mod_mul(c.generator.x, c.p);
    BigNum ax = c.a.mod_mul(c.generator.x, c.p);
    BigNum rhs = x3.mod_add(ax, c.p).mod_add(c.b, c.p);
    ASSERT(c.is_on_curve(c.generator));
})

// SHA-256 tests

TEST(sha256_empty, {
    auto h = SHA256::hash((const u8*)"", 0);
    ASSERT_EQ(h[0], 0xe3);
    ASSERT_EQ(h[1], 0xb0);
})

TEST(sha256_abc, {
    auto h = SHA256::hash((const u8*)"abc", 3);
    ASSERT_EQ(h[0], 0xba);
    ASSERT_EQ(h[1], 0x78);
})

static const u8 hmac_key_2[] = {0x4a,0x65,0x66,0x65};
static const u8 hmac_data_2[] = {0x77,0x68,0x61,0x74,0x20,0x64,0x6f,0x20,0x79,0x61,0x20,0x77,0x61,0x6e,0x74,0x20,0x66,0x6f,0x72,0x20,0x6e,0x6f,0x74,0x68,0x69,0x6e,0x67,0x3f};
static const u8 hmac_expected_2[] = {0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43};

TEST(hmac_sha256, {
    std::vector<u8> key(hmac_key_2, hmac_key_2 + sizeof(hmac_key_2));
    std::vector<u8> data(hmac_data_2, hmac_data_2 + sizeof(hmac_data_2));
    auto h = hmac_sha256(key, data);
    ASSERT_EQ(h.size(), 32u);
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(h[i], hmac_expected_2[i]);
    }
})

TEST(hmac_sha256_empty, {
    std::vector<u8> key;
    std::vector<u8> data;
    auto h = hmac_sha256(key, data);
    ASSERT_EQ(h.size(), 32u);
    ASSERT_EQ(h[0], 0xb6);
})

// Known SHA256 test: "The quick brown fox jumps over the lazy dog"
static const u8 sha256_fox_msg[] = {
    'T','h','e',' ','q','u','i','c','k',' ','b','r','o','w','n',' ',
    'f','o','x',' ','j','u','m','p','s',' ','o','v','e','r',' ','t',
    'h','e',' ','l','a','z','y',' ','d','o','g'
};

static const u8 sha256_fox_expected[] = {
    0xd7,0xa8,0xfb,0xb3,0x07,0xd7,0x80,0x94,
    0x69,0xca,0x9a,0xbc,0xb0,0x08,0x2e,0x4f,
    0x8d,0x56,0x51,0xe4,0x6d,0x3c,0xdb,0x76,
    0x2d,0x02,0xd0,0xbf,0x37,0xc9,0xe5,0x92
};

TEST(sha256_fox, {
    auto h = SHA256::hash(sha256_fox_msg, sizeof(sha256_fox_msg));
    ASSERT_EQ(h.size(), 32u);
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(h[i], sha256_fox_expected[i]);
    }
})

// RFC 4231 Test Case 1
static const u8 hmac_key_1[] = {0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b};
static const u8 hmac_data_1[] = {'H','i',' ','T','h','e','r','e'};
static const u8 hmac_expected_1[] = {0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};

TEST(hmac_sha256_tc1, {
    std::vector<u8> key(hmac_key_1, hmac_key_1 + 20);
    std::vector<u8> data(hmac_data_1, hmac_data_1 + 8);
    auto h = hmac_sha256(key, data);
    ASSERT_EQ(h.size(), 32u);
    ASSERT_EQ(h[0], hmac_expected_1[0]);
})

static const u8 sha256_112msg[] = {
    'a','b','c','d','e','f','g','h',
    'b','c','d','e','f','g','h','i',
    'c','d','e','f','g','h','i','j',
    'd','e','f','g','h','i','j','k',
    'e','f','g','h','i','j','k','l',
    'f','g','h','i','j','k','l','m',
    'g','h','i','j','k','l','m','n',
    'h','i','j','k','l','m','n','o',
    'i','j','k','l','m','n','o','p',
    'j','k','l','m','n','o','p','q',
    'k','l','m','n','o','p','q','r',
    'l','m','n','o','p','q','r','s',
    'm','n','o','p','q','r','s','t',
    'n','o','p','q','r','s','t','u'
};

static const u8 sha256_112_expected[] = {
    0xcf,0x5b,0x16,0xa7,0x78,0xaf,0x83,0x80,
    0x03,0x6c,0xe5,0x9e,0x7b,0x04,0x92,0x37,
    0x0b,0x24,0x9b,0x11,0xe8,0xf0,0x7a,0x51,
    0xaf,0xac,0x45,0x03,0x7a,0xfe,0xe9,0xd1
};

TEST(sha256_2block, {
    auto h = SHA256::hash(sha256_112msg, sizeof(sha256_112msg));
    ASSERT_EQ(h.size(), 32u);
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ(h[i], sha256_112_expected[i]);
    }
})

// HKDF tests

static const u8 hkdf_salt_1[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c};
static const u8 hkdf_info_1[] = {0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9};

TEST(hkdf_basic, {
    // RFC 5869 Test Case 1 (SHA-256)
    std::vector<u8> salt(hkdf_salt_1, hkdf_salt_1 + 13);
    std::vector<u8> ikm(22, 0x0b);
    std::vector<u8> info(hkdf_info_1, hkdf_info_1 + 10);
    auto prk = HKDF::extract(salt, ikm);
    auto result = HKDF::derive(salt, ikm, info, 42);
    ASSERT_EQ(result.size(), 42u);
    // RFC 5869 TC1 PRK = 0x0777..., OKM = 0x3cb2...
    ASSERT_EQ(prk[0], 0x07);
    ASSERT_EQ(result[0], 0x3c);
})

// SHA-384 tests

TEST(sha384_abc, {
    auto h = SHA384::hash((const u8*)"abc", 3);
    ASSERT_EQ(h[0], 0xcb);
    ASSERT_EQ(h[1], 0x00);
})

// X25519 tests

static const u8 x25519_scalar_a[] = {0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a};
static const u8 x25519_scalar_b[] = {0x5d,0xab,0x08,0x7e,0x62,0x4a,0x8a,0x4b,0x79,0xe1,0x7f,0x8b,0x83,0x80,0x0e,0xe6,0x6f,0x3b,0xb1,0x29,0x26,0x18,0xb6,0xfd,0x1c,0x2f,0x8b,0x27,0xff,0x88,0xe0,0xeb};
static const u8 x25519_u[] = {0x85,0x20,0xf0,0x09,0x89,0x30,0xa7,0x54,0x74,0x8b,0x7d,0xdc,0xb4,0x3e,0xf7,0x5a,0x0d,0xbf,0x3a,0x0d,0x26,0x38,0x1a,0xf4,0xeb,0xa4,0xa9,0x8e,0xaa,0x9b,0x4e,0x6a};
static const u8 x25519_expected_ss[] = {0x4a,0x5d,0x9d,0x5b,0xa4,0xce,0x2d,0xe8,0x17,0x28,0xe3,0xbf,0x48,0x03,0x50,0xf2,0x5e,0x07,0xe2,0x1c,0x94,0x7d,0x19,0xe3,0x37,0x6f,0x09,0xb3,0xc1,0xe1,0x61,0x74};

static const u8 x25519_tv1_s[] = {0xa5,0x46,0xe3,0x6b,0xf0,0x52,0x7c,0x9d,0x3b,0x16,0x15,0x4b,0x82,0x46,0x5e,0xdd,0x62,0x14,0x4c,0x0a,0xc1,0xfc,0x5a,0x18,0x50,0x6a,0x22,0x44,0xba,0x44,0x9a,0xc4};
static const u8 x25519_tv1_u[] = {0xe6,0xdb,0x68,0x67,0x58,0x30,0x30,0xdb,0x35,0x94,0xc1,0xa4,0x24,0xb1,0x5f,0x7c,0x72,0x66,0x24,0xec,0x26,0xb3,0x35,0x3b,0x10,0xa9,0x03,0xa6,0xd0,0xab,0x1c,0x4c};
static const u8 x25519_tv1_e[] = {0xc3,0xda,0x55,0x37,0x9d,0xe9,0xc6,0x90,0x8e,0x94,0xea,0x4d,0xf2,0x8d,0x08,0x4f,0x32,0xec,0xcf,0x03,0x49,0x1c,0x71,0xf7,0x54,0xb4,0x07,0x55,0x77,0xa2,0x85,0x52};
static const u8 x25519_tv2_s[] = {0x4b,0x66,0xe9,0xd4,0xd1,0xb4,0x67,0x3c,0x5a,0xd2,0x26,0x91,0x95,0x7d,0x6a,0xf5,0xc1,0x1b,0x64,0x21,0xe0,0xea,0x01,0xd4,0x2c,0xa4,0x16,0x9e,0x79,0x18,0xba,0x0d};
static const u8 x25519_tv2_u[] = {0xe5,0x21,0x0f,0x12,0x78,0x68,0x11,0xd3,0xf4,0xb7,0x95,0x9d,0x05,0x38,0xae,0x2c,0x31,0xdb,0xe7,0x10,0x6f,0xc0,0x3c,0x3e,0xfc,0x4c,0xd5,0x49,0xc7,0x15,0xa4,0x93};
static const u8 x25519_tv2_e[] = {0x95,0xcb,0xde,0x94,0x76,0xe8,0x90,0x7d,0x7a,0xad,0xe4,0x5c,0xb4,0xb8,0x73,0xf8,0x8b,0x59,0x5a,0x68,0x79,0x9f,0xa1,0x52,0xe6,0xf8,0xf7,0x64,0x7a,0xac,0x79,0x57};
TEST(x25519_rfc7748, {
    u8 o1[32]; u8 o2[32];
    X25519::shared_secret(x25519_tv1_s, x25519_tv1_u, o1);
    X25519::shared_secret(x25519_tv2_s, x25519_tv2_u, o2);
    ASSERT_EQ(u32(o1[0]), u32(x25519_tv1_e[0]));
    ASSERT_EQ(u32(o1[1]), u32(x25519_tv1_e[1]));
    ASSERT_EQ(u32(o2[0]), u32(x25519_tv2_e[0]));
    ASSERT_EQ(u32(o2[1]), u32(x25519_tv2_e[1]));
    X25519::shared_secret(x25519_scalar_b, x25519_u, o1);
    ASSERT_EQ(u32(o1[0]), u32(x25519_expected_ss[0]));
    ASSERT_EQ(u32(o1[1]), u32(x25519_expected_ss[1]));
})

// ChaCha20 tests (RFC 8439 Section 2.4.2)

static const u8 chacha_key[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const u8 chacha_nonce[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 chacha_expected_keystream[] = {
    0x76,0xb8,0xe0,0xad,0xa0,0xf1,0x3d,0x90,
    0x40,0x5d,0x6a,0xe5,0x53,0x86,0xbd,0x28,
    0xbd,0xd2,0x19,0xb8,0xa0,0x8d,0xed,0x1a,
    0xa8,0x36,0xef,0xcc,0x8b,0x77,0x0d,0xc7,
    0xda,0x41,0x59,0x7c,0x51,0x57,0x48,0x8d,
    0x77,0x24,0xe0,0x3f,0xb8,0xd8,0x4a,0x37,
    0x6a,0x43,0xb8,0xf4,0x15,0x18,0xa1,0x1c,
    0xc3,0x87,0xb6,0x69,0xb2,0xee,0x65,0x86
};

TEST(chacha20_basic, {
    std::vector<u8> input(64, 0);
    std::vector<u8> output(64);
    ChaCha20 cc;
    cc.set_key(chacha_key);
    cc.set_nonce(chacha_nonce);
    cc.set_counter(0);
    cc.encrypt(input.data(), 64, output.data());
    for (int i = 0; i < 64; i++) {
        ASSERT_EQ(output[i], chacha_expected_keystream[i]);
    }
})

// AES-GCM tests

static const u8 aes_gcm_key[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 aes_gcm_iv[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 aes_gcm_pt[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static const u8 aes_gcm_ct_expected[] = {
    0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,
    0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78
};
TEST(aes_gcm_basic, {
    AES aes;
    aes.set_key(aes_gcm_key, 16);
    aes.set_iv(aes_gcm_iv, 12);
    u8 tag[16];
    auto ct = aes.encrypt_gcm(aes_gcm_pt, 16, (const u8*)"", 0, tag);
    ASSERT_EQ(ct.size(), 16u);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(ct[i], aes_gcm_ct_expected[i]);
    }
})

TEST(aes_gcm_decrypt, {
    AES aes;
    aes.set_key(aes_gcm_key, 16);
    aes.set_iv(aes_gcm_iv, 12);
    u8 tag[16];
    auto ct = aes.encrypt_gcm(aes_gcm_pt, 16, (const u8*)"", 0, tag);
    bool ok = aes.decrypt_gcm(ct.data(), ct.size(), (const u8*)"", 0, tag);
    ASSERT(ok);
    auto decrypted = aes.decrypt_gcm_return(ct.data(), ct.size(), (const u8*)"", 0, tag);
    ASSERT(!decrypted.empty());
    ASSERT_EQ(decrypted.size(), 16u);
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(decrypted[i], aes_gcm_pt[i]);
    }
    u8 bad_tag[16];
    std::memcpy(bad_tag, tag, 16);
    bad_tag[0] ^= 1;
    bool fail = aes.decrypt_gcm(ct.data(), ct.size(), (const u8*)"", 0, bad_tag);
    ASSERT(!fail);
})

// Certificate message parser tests (N-S3: hostile length fields must fail closed)

static std::vector<u8> cert_msg_with_list(const std::vector<u8> &entries, u32 claimed_list_len) {
    std::vector<u8> body;
    body.push_back(0);  // empty certificate_request_context
    body.push_back(static_cast<u8>((claimed_list_len >> 16) & 0xFF));
    body.push_back(static_cast<u8>((claimed_list_len >> 8) & 0xFF));
    body.push_back(static_cast<u8>(claimed_list_len & 0xFF));
    body.insert(body.end(), entries.begin(), entries.end());
    return body;
}

TEST(cert_parse_valid_single, {
    // One entry: 3-byte len + 4-byte DER + 2-byte zero ext block
    std::vector<u8> entry = {0x00, 0x00, 0x04, 0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00};
    auto r = parse_certificate_message(cert_msg_with_list(entry, static_cast<u32>(entry.size())));
    ASSERT(r.is_ok());
    ASSERT_EQ(r.unwrap().size(), 1u);
    ASSERT_EQ(r.unwrap()[0].size(), 4u);
    ASSERT_EQ(r.unwrap()[0][0], 0xAA);
})

TEST(cert_parse_valid_two_entries, {
    std::vector<u8> e1 = {0x00, 0x00, 0x02, 0x01, 0x02, 0x00, 0x00};
    std::vector<u8> e2 = {0x00, 0x00, 0x03, 0x05, 0x06, 0x07, 0x00, 0x00};
    std::vector<u8> both;
    both.insert(both.end(), e1.begin(), e1.end());
    both.insert(both.end(), e2.begin(), e2.end());
    auto r = parse_certificate_message(cert_msg_with_list(both, static_cast<u32>(both.size())));
    ASSERT(r.is_ok());
    ASSERT_EQ(r.unwrap().size(), 2u);
})

// The N-S3 repro: claimed list_len far beyond the actual body. Pre-fix this read
// past the buffer (loop bounded against list_end while dereferencing body[off]).
TEST(cert_parse_oversized_list_len_rejected, {
    std::vector<u8> entry = {0x00, 0x00, 0x04, 0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00};
    auto r = parse_certificate_message(cert_msg_with_list(entry, 0xFFFFFFu));
    ASSERT(r.is_err());
})

TEST(cert_parse_truncated_context_rejected, {
    std::vector<u8> body = {0x40};  // ctx_len=64 with no context bytes present
    auto r = parse_certificate_message(body);
    ASSERT(r.is_err());
})

TEST(cert_parse_zero_len_cert_rejected, {
    std::vector<u8> entry = {0x00, 0x00, 0x00, 0x00, 0x00};
    auto r = parse_certificate_message(cert_msg_with_list(entry, static_cast<u32>(entry.size())));
    ASSERT(r.is_err());
})

TEST(cert_parse_empty_body_rejected, {
    std::vector<u8> body;
    auto r = parse_certificate_message(body);
    ASSERT(r.is_err());
})

TEST(cert_parse_stray_tail_rejected, {
    // Valid single entry followed by a stray byte inside the claimed list
    std::vector<u8> entry = {0x00, 0x00, 0x02, 0x01, 0x02, 0x00, 0x00};
    std::vector<u8> padded = entry;
    padded.push_back(0x99);
    auto r = parse_certificate_message(cert_msg_with_list(padded, static_cast<u32>(padded.size())));
    ASSERT(r.is_err());
})

TEST(cert_parse_missing_ext_block_rejected, {
    // Entry without the trailing 2-byte extension block
    std::vector<u8> entry = {0x00, 0x00, 0x02, 0x01, 0x02};
    auto r = parse_certificate_message(cert_msg_with_list(entry, static_cast<u32>(entry.size())));
    ASSERT(r.is_err());
})

// Certificate chain validator tests (N-S2: every failure path must fail closed)

TEST(cert_chain_empty_rejected, {
    CertValidationResult r;
    validate_certificate_chain({}, "example.com", r);
    ASSERT(!r.is_valid());
    ASSERT_EQ(static_cast<int>(r.result), static_cast<int>(CertResult::MALFORMED));
})

// Wildcard matching: exactly one label (N-S2). check_hostname_match is static, so
// exercise it through the public API is impossible without a live cert; instead test
// via the exported validator on garbage DER which must also be rejected.
TEST(cert_chain_garbage_der_rejected, {
    std::vector<std::vector<u8>> chain = {{0xDE, 0xAD, 0xBE, 0xEF}};
    CertValidationResult r;
    validate_certificate_chain(chain, "example.com", r);
    ASSERT(!r.is_valid());
    ASSERT_EQ(static_cast<int>(r.result), static_cast<int>(CertResult::MALFORMED));
})

TEST(cert_chain_truncated_der_rejected, {
    // ASN.1 SEQUENCE header claiming more bytes than provided
    std::vector<std::vector<u8>> chain = {{0x30, 0x82, 0xFF, 0xFF, 0x01}};
    CertValidationResult r;
    validate_certificate_chain(chain, "example.com", r);
    ASSERT(!r.is_valid());
})

// CertificateVerify signature verification (N-S1)

static void der_len(std::vector<u8> &out, size_t len) {
    if (len < 128) {
        out.push_back((u8)len);
        return;
    }
    int nb = (len <= 0xFF) ? 1 : (len <= 0xFFFF ? 2 : 3);
    out.push_back((u8)(0x80 | nb));
    for (int i = nb - 1; i >= 0; i--) out.push_back((u8)((len >> (8 * i)) & 0xFF));
}

static void der_int(std::vector<u8> &out, const std::vector<u8> &v) {
    out.push_back(0x02);
    size_t start = 0;
    while (start < v.size() && v[start] == 0) start++;
    size_t vl = v.size() - start;
    bool pad = vl == 0 || (v[start] & 0x80);
    der_len(out, vl + (pad ? 1 : 0));
    if (pad)
        out.push_back(0);
    out.insert(out.end(), v.begin() + start, v.end());
}

// Minimal structurally-valid X.509 shell around a SubjectPublicKeyInfo.
static std::vector<u8> make_cert_with_spki(const std::vector<u8> &spki) {
    auto seq = [](const std::vector<u8> &inner) {
        std::vector<u8> out{(u8)0x30};
        der_len(out, inner.size());
        out.insert(out.end(), inner.begin(), inner.end());
        return out;
    };
    std::vector<u8> tbs;
    tbs.push_back(0x02);
    tbs.push_back(0x01);
    tbs.push_back(0x01);  // serialNumber
    for (int i = 0; i < 4; i++) {
        tbs.push_back(0x30);
        tbs.push_back(0x00);
    }  // sig/issuer/validity/subject
    tbs.insert(tbs.end(), spki.begin(), spki.end());
    std::vector<u8> rest = seq(tbs);
    rest.push_back(0x30);
    rest.push_back(0x00);  // signatureAlgorithm
    rest.push_back(0x03);
    rest.push_back(0x02);
    rest.push_back(0x00);
    rest.push_back(0x00);  // signatureValue
    return seq(rest);
}

static std::vector<u8> make_ec_p256_spki(const std::vector<u8> &point) {
    static const u8 oid_ec[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
    static const u8 oid_p256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
    std::vector<u8> algid_inner{(u8)0x06, sizeof(oid_ec)};
    algid_inner.insert(algid_inner.end(), oid_ec, oid_ec + sizeof(oid_ec));
    algid_inner.push_back((u8)0x06);
    algid_inner.push_back(sizeof(oid_p256));
    algid_inner.insert(algid_inner.end(), oid_p256, oid_p256 + sizeof(oid_p256));

    std::vector<u8> bitstr{(u8)0x03};
    der_len(bitstr, point.size() + 1);
    bitstr.push_back(0);
    bitstr.insert(bitstr.end(), point.begin(), point.end());

    std::vector<u8> spki_inner{(u8)0x30};
    der_len(spki_inner, algid_inner.size());
    spki_inner.insert(spki_inner.end(), algid_inner.begin(), algid_inner.end());
    spki_inner.insert(spki_inner.end(), bitstr.begin(), bitstr.end());

    std::vector<u8> spki{(u8)0x30};
    der_len(spki, spki_inner.size());
    spki.insert(spki.end(), spki_inner.begin(), spki_inner.end());
    return spki;
}

TEST(cert_verify_extract_ec_key, {
    auto curve = EllipticCurve::secp256r1();
    ECPoint q = curve.point_mul(curve.generator, BigNum(758883078u));
    std::vector<u8> point;
    point.push_back(0x04);
    auto xb = q.x.to_bytes(32);
    auto yb = q.y.to_bytes(32);
    point.insert(point.end(), xb.begin(), xb.end());
    point.insert(point.end(), yb.begin(), yb.end());

    PublicKey pk;
    ASSERT(extract_certificate_public_key(make_cert_with_spki(make_ec_p256_spki(point)), pk));
    ASSERT_EQ(static_cast<int>(pk.kind), static_cast<int>(PubKeyKind::EC_P256));
    ASSERT(pk.point == point);
})

// Self-consistency: sign the CV content with our own ECC primitives and verify.
TEST(cert_verify_ecdsa_roundtrip, {
    auto curve = EllipticCurve::secp256r1();
    BigNum d(0x1234567Au);
    ECPoint q = curve.point_mul(curve.generator, d);

    std::vector<u8> point;
    point.push_back(0x04);
    auto xb = q.x.to_bytes(32);
    auto yb = q.y.to_bytes(32);
    point.insert(point.end(), xb.begin(), xb.end());
    point.insert(point.end(), yb.begin(), yb.end());

    std::vector<u8> th(32, 0xCD);
    auto content = make_certificate_verify_content("TLS 1.3, server CertificateVerify", th);
    auto digest = SHA256::hash(content.data(), content.size());

    // Sign: k fixed nonce; r = (kG).x mod n ; s = k^-1(e + d r) mod n
    BigNum k(0x0B0B0FACu);
    ECPoint kg = curve.point_mul(curve.generator, k);
    BigNum r = kg.x.mod_mul(BigNum(1), curve.order);
    BigNum e = BigNum::from_bytes(digest.data(), digest.size());
    BigNum dr = d.mod_mul(r, curve.order);
    BigNum s = k.mod_inverse(curve.order).mod_mul(e.mod_add(dr, curve.order), curve.order);

    std::vector<u8> sig_inner;
    der_int(sig_inner, r.to_bytes(32));
    der_int(sig_inner, s.to_bytes(32));
    std::vector<u8> sig{(u8)0x30};
    der_len(sig, sig_inner.size());
    sig.insert(sig.end(), sig_inner.begin(), sig_inner.end());

    std::vector<std::vector<u8>> chain{make_cert_with_spki(make_ec_p256_spki(point))};
    auto vr = verify_server_certificate_verify(chain, 0x0403, sig, th);
    ASSERT_EQ(static_cast<int>(vr), static_cast<int>(CVResult::VALID));

    sig[5] ^= 0x01;
    vr = verify_server_certificate_verify(chain, 0x0403, sig, th);
    ASSERT_EQ(static_cast<int>(vr), static_cast<int>(CVResult::INVALID_SIGNATURE));
})

TEST(cert_verify_unsupported_alg_rejected, {
    std::vector<std::vector<u8>> chain{make_cert_with_spki(make_ec_p256_spki(std::vector<u8>(65, 0x04)))};
    auto vr = verify_server_certificate_verify(chain, 0x0501, std::vector<u8>(8, 0), std::vector<u8>(32, 0));
    ASSERT_EQ(static_cast<int>(vr), static_cast<int>(CVResult::UNSUPPORTED_ALGORITHM));
})

TEST(cert_verify_no_public_key_rejected, {
    auto vr = verify_server_certificate_verify({}, 0x0403, std::vector<u8>(8, 0), std::vector<u8>(32, 0));
    ASSERT_EQ(static_cast<int>(vr), static_cast<int>(CVResult::NO_PUBLIC_KEY));
})

// TLS connect test

TEST(tls_connect_google, {
    auto conn = std::make_unique<net::Connection>();
    auto r = conn->open("google.com", 443);
    if (r.is_err()) return true;
    auto tls = std::make_unique<TLSConnection>();
    auto tls_r = tls->connect(conn.get(), "google.com");
    ASSERT(tls_r.is_ok());
    const char* req = "GET / HTTP/1.1\r\nHost: google.com\r\nConnection: close\r\n\r\n";
    tls->send_all((const u8*)req, static_cast<u32>(std::strlen(req)));
    auto resp = tls->receive_all();
    ASSERT(resp.is_ok());
    ASSERT(resp.unwrap().size() > 0);
    tls->close();
})

// ---- N-C2/N-C6 regression: Poly1305 must match RFC 8439 test vectors.
// The previous implementation stored r in four 26-bit limbs (dropping r's
// high bits), polluted limbs with oversized masks, and applied the full-block
// pad bit to partial tails — every tag over one block was wrong, which made
// TLS_CHACHA20_POLY1305 handshakes fail and hid real server alerts.

static bool poly1305_tag_matches(const char *key_hex, const unsigned char *msg, size_t len, const char *tag_hex) {
    u8 key[32];
    for (int i = 0; i < 32; i++) {
        unsigned int b = 0;
        if (sscanf(key_hex + i * 2, "%2x", &b) != 1)
            return false;
        key[i] = static_cast<u8>(b);
    }
    Poly1305 p;
    p.set_key(key);
    if (len > 0)
        p.update(msg, len);
    u8 mac[16];
    p.finish(mac);
    for (int i = 0; i < 16; i++) {
        unsigned int b = 0;
        if (sscanf(tag_hex + i * 2, "%2x", &b) != 1)
            return false;
        if (mac[i] != static_cast<u8>(b))
            return false;
    }
    return true;
}

static const char kPolyKey[] = "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b";

TEST(poly1305_rfc8439_vector_multi_block, {
    // RFC 8439 section 2.5.2 (34 bytes: two full blocks + a partial tail).
    ASSERT(poly1305_tag_matches(kPolyKey,
                                reinterpret_cast<const u8 *>("Cryptographic Forum Research Group"),
                                34,
                                "a8061dc1305136c6c22b8baf0c0127a9"));
})

TEST(poly1305_rfc8439_vector_empty_message, {
    // Empty message: tag is just s.
    ASSERT(poly1305_tag_matches(kPolyKey, nullptr, 0, "0103808afb0db2fd4abff6af4149f51b"));
})

TEST(poly1305_rfc8439_vector_exact_block, {
    // One exact 16-byte block.
    const unsigned char block[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    ASSERT(poly1305_tag_matches(kPolyKey, block, sizeof(block), "a18a0de2ba299128303a398e28bde4f0"));
})
