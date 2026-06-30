#include "../crypto/aes.hpp"
#include "../crypto/chacha20.hpp"
#include "../crypto/poly1305.hpp"
#include "connection.hpp"

#include <cstring>

namespace browser::net::tls {

    // ChaCha20-Poly1305 AEAD encryption
    static std::vector<u8> chacha20_poly1305_encrypt_impl(
        const u8 key[32], const u8 iv[12], u64 seq, const u8 *plaintext, u32 pt_len, u8 content_type) {
        std::vector<u8> pt(plaintext, plaintext + pt_len);
        pt.push_back(content_type);

        u8 nonce[12];
        std::memcpy(nonce, iv, 12);
        for (int i = 0; i < 8; i++) nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);

        crypto::ChaCha20 cc_poly;
        cc_poly.set_key(key);
        cc_poly.set_nonce(nonce);
        cc_poly.set_counter(0);
        u8 poly_key[32] = {};
        u8 zero_block[32] = {};
        cc_poly.encrypt(zero_block, 32, poly_key);

        crypto::ChaCha20 cc_enc;
        cc_enc.set_key(key);
        cc_enc.set_nonce(nonce);
        cc_enc.set_counter(1);
        std::vector<u8> ct(pt.size());
        cc_enc.encrypt(pt.data(), pt.size(), ct.data());

        u8 aad[5];
        aad[0] = 0x17;
        aad[1] = 0x03;
        aad[2] = 0x03;
        u16 ct_full_len = static_cast<u16>(ct.size() + 16);
        aad[3] = static_cast<u8>((ct_full_len >> 8) & 0xFF);
        aad[4] = static_cast<u8>(ct_full_len & 0xFF);

        crypto::Poly1305 mac;
        mac.set_key(poly_key);
        mac.update(aad, 5);
        u8 zero_pad[16] = {0};
        std::size_t aad_rem = 5 % 16;
        if (aad_rem)
            mac.update(zero_pad, 16 - aad_rem);
        mac.update(ct.data(), ct.size());
        std::size_t ct_rem = ct.size() % 16;
        if (ct_rem)
            mac.update(zero_pad, 16 - ct_rem);
        u8 len_block[16] = {0};
        u64 aad_len = 5;
        len_block[0] = static_cast<u8>(aad_len);
        len_block[1] = static_cast<u8>(aad_len >> 8);
        len_block[2] = static_cast<u8>(aad_len >> 16);
        len_block[3] = static_cast<u8>(aad_len >> 24);
        u64 enc_len = ct.size();
        len_block[8] = static_cast<u8>(enc_len);
        len_block[9] = static_cast<u8>(enc_len >> 8);
        len_block[10] = static_cast<u8>(enc_len >> 16);
        len_block[11] = static_cast<u8>(enc_len >> 24);
        mac.update(len_block, 16);

        u8 tag[16];
        mac.finish(tag);
        ct.insert(ct.end(), tag, tag + 16);
        return ct;
    }

    // ChaCha20-Poly1305 AEAD decryption
    static std::vector<u8> chacha20_poly1305_decrypt_impl(
        const u8 key[32], const u8 iv[12], u64 seq, const u8 *ciphertext, u32 ct_len, u8 &content_type) {
        if (ct_len < 16) {
            content_type = 0;
            return {};
        }
        u32 encrypted_len = ct_len - 16;
        const u8 *tag = ciphertext + encrypted_len;

        u8 nonce[12];
        std::memcpy(nonce, iv, 12);
        for (int i = 0; i < 8; i++) nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);

        crypto::ChaCha20 cc_poly;
        cc_poly.set_key(key);
        cc_poly.set_nonce(nonce);
        cc_poly.set_counter(0);
        u8 poly_key[32] = {};
        u8 zero_block[32] = {};
        cc_poly.encrypt(zero_block, 32, poly_key);

        u8 aad[5];
        aad[0] = 0x17;
        aad[1] = 0x03;
        aad[2] = 0x03;
        aad[3] = static_cast<u8>((ct_len >> 8) & 0xFF);
        aad[4] = static_cast<u8>(ct_len & 0xFF);

        crypto::Poly1305 mac;
        mac.set_key(poly_key);
        mac.update(aad, 5);
        u8 zero_pad[16] = {0};
        std::size_t aad_rem = 5 % 16;
        if (aad_rem)
            mac.update(zero_pad, 16 - aad_rem);
        mac.update(ciphertext, encrypted_len);
        std::size_t ct_rem = encrypted_len % 16;
        if (ct_rem)
            mac.update(zero_pad, 16 - ct_rem);
        u8 len_block[16] = {0};
        u64 aad_len = 5;
        len_block[0] = static_cast<u8>(aad_len);
        len_block[1] = static_cast<u8>(aad_len >> 8);
        len_block[2] = static_cast<u8>(aad_len >> 16);
        len_block[3] = static_cast<u8>(aad_len >> 24);
        u64 enc_len = encrypted_len;
        len_block[8] = static_cast<u8>(enc_len);
        len_block[9] = static_cast<u8>(enc_len >> 8);
        len_block[10] = static_cast<u8>(enc_len >> 16);
        len_block[11] = static_cast<u8>(enc_len >> 24);
        mac.update(len_block, 16);

        u8 computed_tag[16];
        mac.finish(computed_tag);
        u8 diff = 0;
        for (int i = 0; i < 16; i++) diff |= computed_tag[i] ^ tag[i];
        if (diff != 0) {
            content_type = 0;
            return {};
        }

        crypto::ChaCha20 cc_enc;
        cc_enc.set_key(key);
        cc_enc.set_nonce(nonce);
        cc_enc.set_counter(1);
        std::vector<u8> pt(encrypted_len);
        cc_enc.encrypt(ciphertext, encrypted_len, pt.data());

        content_type = pt.back();
        pt.pop_back();
        return pt;
    }

    std::vector<u8> TLSConnection::aead_encrypt(
        const u8 key[32], const u8 iv[12], u64 seq, const u8 *plaintext, u32 pt_len, u8 content_type) {
        if (cipher_suite_ == 0x1303)
            return chacha20_poly1305_encrypt_impl(key, iv, seq, plaintext, pt_len, content_type);

        std::vector<u8> pt(plaintext, plaintext + pt_len);
        pt.push_back(content_type);

        u8 nonce[12];
        std::memcpy(nonce, iv, 12);
        for (int i = 0; i < 8; i++) {
            nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);
        }

        u8 aad[5];
        aad[0] = 0x17;
        aad[1] = 0x03;
        aad[2] = 0x03;
        u16 ct_len = static_cast<u16>(pt.size() + 16);
        aad[3] = static_cast<u8>((ct_len >> 8) & 0xFF);
        aad[4] = static_cast<u8>(ct_len & 0xFF);

        aes_encrypt_.set_iv(nonce, 12);
        u8 tag[16];
        auto ct = aes_encrypt_.encrypt_gcm(pt.data(), pt.size(), aad, 5, tag);

        ct.insert(ct.end(), tag, tag + 16);
        return ct;
    }

    std::vector<u8> TLSConnection::aead_decrypt(
        const u8 key[32], const u8 iv[12], u64 seq, const u8 *ciphertext, u32 ct_len, u8 &content_type) {
        if (cipher_suite_ == 0x1303)
            return chacha20_poly1305_decrypt_impl(key, iv, seq, ciphertext, ct_len, content_type);
        if (ct_len < 16) {
            content_type = 0;
            return {};
        }

        u32 encrypted_len = ct_len - 16;
        const u8 *tag = ciphertext + encrypted_len;

        u8 nonce[12];
        std::memcpy(nonce, iv, 12);
        for (int i = 0; i < 8; i++) {
            nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);
        }

        u8 aad[5];
        aad[0] = 0x17;
        aad[1] = 0x03;
        aad[2] = 0x03;
        aad[3] = static_cast<u8>((ct_len >> 8) & 0xFF);
        aad[4] = static_cast<u8>(ct_len & 0xFF);

        aes_decrypt_.set_iv(nonce, 12);
        auto pt = aes_decrypt_.decrypt_gcm_return(ciphertext, encrypted_len, aad, 5, tag);
        if (pt.empty()) {
            content_type = 0;
            return {};
        }

        content_type = pt.back();
        pt.pop_back();
        return pt;
    }

}  // namespace browser::net::tls
