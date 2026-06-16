#include "tls.hpp"
#include "crypto/x25519.hpp"
#include "crypto/sha.hpp"
#include "crypto/aes.hpp"
#include "crypto/chacha20.hpp"
#include "crypto/poly1305.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

namespace browser::net::tls {

TLSConnection::TLSConnection() = default;
TLSConnection::~TLSConnection() { close(); }

void TLSConnection::reset_state() {
    connected_ = false;
    app_keys_set_ = false;
    client_seq_ = 0;
    server_seq_ = 0;
    cipher_suite_ = 0;
    transcript_.clear();
    transcript_hasher_.reset();
    alpn_.clear();
    std::memset(client_priv_, 0, 32);
    std::memset(client_pub_, 0, 32);
    aes_encrypt_ = crypto::AES();
    aes_decrypt_ = crypto::AES();
    std::memset(server_hs_key_, 0, 32);
    std::memset(server_hs_iv_, 0, 12);
    std::memset(client_hs_key_, 0, 32);
    std::memset(client_hs_iv_, 0, 12);
    std::memset(server_app_key_, 0, 32);
    std::memset(server_app_iv_, 0, 12);
    std::memset(client_app_key_, 0, 32);
    std::memset(client_app_iv_, 0, 12);
    server_hs_traffic_.clear();
    client_hs_traffic_.clear();
    server_app_traffic_.clear();
    client_app_traffic_.clear();
}

std::string TLSConnection::negotiated_alpn() const {
    return alpn_;
}

bool TLSConnection::is_connected() const {
    return connected_ && tcp_ && tcp_->is_open();
}

void TLSConnection::close() {
    reset_state();
    if (tcp_) {
        tcp_->close();
        tcp_ = nullptr;
    }
}

// Build TLS record: type || 0x0303 || length(2) || data
static std::vector<u8> make_record(u8 type, const std::vector<u8>& data) {
    std::vector<u8> record;
    record.push_back(type);
    record.push_back(0x03);
    record.push_back(0x03);
    record.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
    record.push_back(static_cast<u8>(data.size() & 0xFF));
    record.insert(record.end(), data.begin(), data.end());
    return record;
}

Result<void> TLSConnection::send_raw_record(u8 type, const std::vector<u8>& data) {
    if (!tcp_) return std::string("no connection");
    auto record = make_record(type, data);
    return tcp_->send_all(record.data(), static_cast<u32>(record.size()));
}

async::task<bool> TLSConnection::send_raw_record_async(u8 type, const std::vector<u8>& data) {
    if (!tcp_) co_return std::string("no connection");
    auto record = make_record(type, data);
    auto r = co_await tcp_->send_all_async(record.data(), static_cast<u32>(record.size()));
    co_return r;
}

Result<std::vector<u8>> TLSConnection::read_raw_record(u8* out_type) {
    u8 header[5];
    {
        u32 hgot = 0;
        while (hgot < 5) {
            auto r = tcp_->receive(header + hgot, 5 - hgot);
            if (r.is_err()) return std::string("read header: " + r.unwrap_err());
            u32 n = r.unwrap();
            if (n == 0) return std::string("connection closed during header read");
            hgot += n;
        }
    }

    u8 type = header[0];
    u16 version = (static_cast<u16>(header[1]) << 8) | header[2];
    u16 length = (static_cast<u16>(header[3]) << 8) | header[4];
    if (version != 0x0303) return std::string("bad record version");

    if (out_type) *out_type = type;

    if (length > 16640) return std::string("record too large: " + std::to_string(length));

    if (length == 0) return std::vector<u8>();

    std::vector<u8> data(length);
    u32 got = 0;
    while (got < length) {
        auto rr = tcp_->receive(data.data() + got, length - got);
        if (rr.is_err()) return std::string("read data: " + rr.unwrap_err());
        u32 n = rr.unwrap();
        if (n == 0) return std::string("connection closed during data read");
        got += n;
    }

    return data;
}

async::task<std::vector<u8>> TLSConnection::read_raw_record_async(u8* out_type) {
    u8 header[5];
    {
        u32 hgot = 0;
        while (hgot < 5) {
            auto r = co_await tcp_->receive_async(header + hgot, 5 - hgot);
            if (r.is_err()) co_return std::string("read header: ") + r.unwrap_err();
            u32 n = r.unwrap();
            if (n == 0) co_return std::string("connection closed during header read");
            hgot += n;
        }
    }

    u8 type = header[0];
    u16 version = (static_cast<u16>(header[1]) << 8) | header[2];
    u16 length = (static_cast<u16>(header[3]) << 8) | header[4];
    if (version != 0x0303) co_return std::string("bad record version");

    if (out_type) *out_type = type;

    if (length > 16640) co_return std::string("record too large: ") + std::to_string(length);

    if (length == 0) co_return std::vector<u8>();

    std::vector<u8> data(length);
    u32 got = 0;
    while (got < length) {
        auto rr = co_await tcp_->receive_async(data.data() + got, length - got);
        if (rr.is_err()) co_return std::string("read data: ") + rr.unwrap_err();
        u32 n = rr.unwrap();
        if (n == 0) co_return std::string("connection closed during data read");
        got += n;
    }

    co_return data;
}

Result<void> TLSConnection::send_encrypted_record(u8 inner_type, const std::vector<u8>& data,
                                                   const u8 key[16], const u8 iv[12], u64& seq) {
    auto ct = aead_encrypt(key, iv, seq, data.data(), static_cast<u32>(data.size()), inner_type);
    seq++;
    auto r = send_raw_record(APPLICATION_DATA, ct);
    if (r.is_err()) return r;
    return {};
}

Result<std::vector<u8>> TLSConnection::read_encrypted_record(const u8 key[16], const u8 iv[12], u64& seq) {
    u8 type = 0;
    auto r = read_raw_record(&type);
    if (r.is_err()) return std::string("read encrypted: " + r.unwrap_err());
    auto& ct = r.unwrap();
    if (ct.empty()) return std::vector<u8>();

    // Handle change_cipher_spec (TLS 1.3 interoperability)
    if (type == CHANGE_CIPHER_SPEC) {
        // Skip it, read next record
        return read_encrypted_record(key, iv, seq);
    }

    u8 inner_type = 0;
    auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
    seq++;
    if (pt.empty() && inner_type == 0) return std::string("decryption failed");

    // The inner type is appended as the last byte
    if (pt.empty()) return std::vector<u8>();

    return pt;
}

async::task<bool> TLSConnection::send_encrypted_record_async(u8 inner_type, const std::vector<u8>& data,
                                                             const u8 key[16], const u8 iv[12], u64& seq) {
    auto ct = aead_encrypt(key, iv, seq, data.data(), static_cast<u32>(data.size()), inner_type);
    seq++;
    auto r = co_await send_raw_record_async(APPLICATION_DATA, ct);
    co_return r;
}

async::task<std::vector<u8>> TLSConnection::read_encrypted_record_async(const u8 key[16], const u8 iv[12], u64& seq) {
    u8 type = 0;
    auto r = co_await read_raw_record_async(&type);
    if (r.is_err()) co_return std::string("read encrypted: ") + r.unwrap_err();
    auto ct = r.unwrap();
    if (ct.empty()) co_return std::vector<u8>();

    if (type == CHANGE_CIPHER_SPEC) {
        auto r2 = co_await read_encrypted_record_async(key, iv, seq);
        co_return r2;
    }

    u8 inner_type = 0;
    auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
    seq++;
    if (pt.empty() && inner_type == 0) co_return std::string("decryption failed");

    co_return pt;
}

// Write a handshake message: type(1) || length(3) || body
static std::vector<u8> make_handshake_msg(u8 type, const std::vector<u8>& body) {
    std::vector<u8> msg;
    msg.push_back(type);
    msg.push_back(static_cast<u8>((body.size() >> 16) & 0xFF));
    msg.push_back(static_cast<u8>((body.size() >> 8) & 0xFF));
    msg.push_back(static_cast<u8>(body.size() & 0xFF));
    msg.insert(msg.end(), body.begin(), body.end());
    return msg;
}

// Parse a handshake message header from data starting at offset
// Returns (type, body_length) on success
struct ParsedHS {
    u8 type;
    u32 body_len;
};
static bool parse_hs_header(const std::vector<u8>& data, std::size_t offset, ParsedHS& out) {
    if (offset + 4 > data.size()) return false;
    out.type = data[offset];
    out.body_len = (static_cast<u32>(data[offset + 1]) << 16) |
                   (static_cast<u32>(data[offset + 2]) << 8) |
                   static_cast<u32>(data[offset + 3]);
    return true;
}

void TLSConnection::append_handshake_to_transcript(u8 type, const std::vector<u8>& body) {
    auto msg = make_handshake_msg(type, body);
    transcript_.insert(transcript_.end(), msg.begin(), msg.end());
    transcript_hasher_.update(msg.data(), msg.size());
}

std::vector<u8> TLSConnection::compute_transcript_hash() const {
    crypto::SHA256 copy(transcript_hasher_);
    return copy.digest();
}

std::vector<u8> TLSConnection::hkdf_expand_label(const std::vector<u8>& secret,
                                                   const std::string& label,
                                                   const std::vector<u8>& context,
                                                   u32 length) {
    // HkdfLabel = u16 length || u8 label_len || "tls13 " + label || u8 context_len || context
    std::vector<u8> info;
    // Length (2 bytes, big-endian)
    info.push_back(static_cast<u8>((length >> 8) & 0xFF));
    info.push_back(static_cast<u8>(length & 0xFF));
    // Label length
    std::string full_label = "tls13 " + label;
    info.push_back(static_cast<u8>(full_label.size()));
    // Label bytes
    info.insert(info.end(), full_label.begin(), full_label.end());
    // Context length
    info.push_back(static_cast<u8>(context.size()));
    // Context bytes
    info.insert(info.end(), context.begin(), context.end());

    return crypto::HKDF::expand(secret, info, length);
}

void TLSConnection::derive_handshake_keys(const std::vector<u8>& shared_secret) {
    // early_secret = HKDF-Extract(salt=0^32, ikm=0^32)
    std::vector<u8> zero_salt(32, 0);
    std::vector<u8> zero_ikm(32, 0);
    std::vector<u8> early_secret = crypto::HKDF::extract(zero_salt, zero_ikm);

    // Derive-Secret(Early_Secret, "derived", "") = HKDF-Expand-Label(Early_Secret, "derived", SHA256(""), 32)
    auto empty_hash = crypto::SHA256::hash((const u8*)"", 0);
    auto derived = hkdf_expand_label(early_secret, "derived", empty_hash, 32);

    // handshake_secret = HKDF-Extract(salt=Derive-Secret(early_secret, "derived", ""), ikm=shared_secret)
    std::vector<u8> hs_secret = crypto::HKDF::extract(derived, shared_secret);

    // Copy to member
    std::memcpy(handshake_secret_, hs_secret.data(), 32);

    // Derive traffic secrets
    auto hs_hash = compute_transcript_hash();
    client_hs_traffic_ = hkdf_expand_label(hs_secret, "c hs traffic", hs_hash, 32);
    server_hs_traffic_ = hkdf_expand_label(hs_secret, "s hs traffic", hs_hash, 32);

    // Derive keys (16 for AES-128-GCM, 32 for ChaCha20-Poly1305)
    u32 hs_key_size = (cipher_suite_ == 0x1303) ? 32 : 16;
    auto c_key = hkdf_expand_label(client_hs_traffic_, "key", {}, hs_key_size);
    auto c_iv = hkdf_expand_label(client_hs_traffic_, "iv", {}, 12);
    auto s_key = hkdf_expand_label(server_hs_traffic_, "key", {}, hs_key_size);
    auto s_iv = hkdf_expand_label(server_hs_traffic_, "iv", {}, 12);

    std::memcpy(client_hs_key_, c_key.data(), hs_key_size);
    if (hs_key_size < 32) std::memset(client_hs_key_ + hs_key_size, 0, 32 - hs_key_size);
    std::memcpy(client_hs_iv_, c_iv.data(), 12);
    std::memcpy(server_hs_key_, s_key.data(), hs_key_size);
    if (hs_key_size < 32) std::memset(server_hs_key_ + hs_key_size, 0, 32 - hs_key_size);
    std::memcpy(server_hs_iv_, s_iv.data(), 12);

    // Pre-set AES key schedules for handshake phase
    if (cipher_suite_ != 0x1303) {
        aes_encrypt_.set_key(client_hs_key_, 16);
        aes_decrypt_.set_key(server_hs_key_, 16);
    }
}

void TLSConnection::derive_application_keys() {
    // Derive-Secret(Handshake_Secret, "derived", "") = HKDF-Expand-Label(Handshake_Secret, "derived", SHA256(""), 32)
    std::vector<u8> zero_ikm(32, 0);
    std::vector<u8> hs_secret(handshake_secret_, handshake_secret_ + 32);
    auto empty_hash = crypto::SHA256::hash((const u8*)"", 0);
    auto derived = hkdf_expand_label(hs_secret, "derived", empty_hash, 32);

    // master_secret = HKDF-Extract(salt=Derive-Secret(handshake_secret, "derived", ""), ikm=0^32)
    std::vector<u8> master_secret = crypto::HKDF::extract(derived, zero_ikm);

    // Derive traffic secrets
    auto hs_hash = compute_transcript_hash();
    client_app_traffic_ = hkdf_expand_label(master_secret, "c ap traffic", hs_hash, 32);
    server_app_traffic_ = hkdf_expand_label(master_secret, "s ap traffic", hs_hash, 32);

    // Derive keys (16 for AES-128-GCM, 32 for ChaCha20-Poly1305)
    u32 app_key_size = (cipher_suite_ == 0x1303) ? 32 : 16;
    auto c_key = hkdf_expand_label(client_app_traffic_, "key", {}, app_key_size);
    auto c_iv = hkdf_expand_label(client_app_traffic_, "iv", {}, 12);
    auto s_key = hkdf_expand_label(server_app_traffic_, "key", {}, app_key_size);
    auto s_iv = hkdf_expand_label(server_app_traffic_, "iv", {}, 12);

    std::memcpy(client_app_key_, c_key.data(), app_key_size);
    if (app_key_size < 32) std::memset(client_app_key_ + app_key_size, 0, 32 - app_key_size);
    std::memcpy(client_app_iv_, c_iv.data(), 12);
    std::memcpy(server_app_key_, s_key.data(), app_key_size);
    if (app_key_size < 32) std::memset(server_app_key_ + app_key_size, 0, 32 - app_key_size);
    std::memcpy(server_app_iv_, s_iv.data(), 12);

    // Re-set AES key schedules for application data phase
    if (cipher_suite_ != 0x1303) {
        aes_encrypt_.set_key(client_app_key_, 16);
        aes_decrypt_.set_key(server_app_key_, 16);
    }

    app_keys_set_ = true;
}

static std::vector<u8> chacha20_poly1305_encrypt_impl(const u8 key[32], const u8 iv[12], u64 seq,
                                                       const u8* plaintext, u32 pt_len,
                                                       u8 content_type) {
    std::vector<u8> pt(plaintext, plaintext + pt_len);
    pt.push_back(content_type);

    u8 nonce[12];
    std::memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);

    crypto::ChaCha20 cc_poly;
    cc_poly.set_key(key);
    cc_poly.set_nonce(nonce);
    cc_poly.set_counter(0);
    u8 poly_key[32];
    cc_poly.encrypt(poly_key, 32, poly_key);

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
    if (aad_rem) mac.update(zero_pad, 16 - aad_rem);
    mac.update(ct.data(), ct.size());
    std::size_t ct_rem = ct.size() % 16;
    if (ct_rem) mac.update(zero_pad, 16 - ct_rem);
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

static std::vector<u8> chacha20_poly1305_decrypt_impl(const u8 key[32], const u8 iv[12], u64 seq,
                                                       const u8* ciphertext, u32 ct_len,
                                                       u8& content_type) {
    if (ct_len < 16) { content_type = 0; return {}; }
    u32 encrypted_len = ct_len - 16;
    const u8* tag = ciphertext + encrypted_len;

    u8 nonce[12];
    std::memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[4 + i] ^= static_cast<u8>((seq >> (56 - i * 8)) & 0xFF);

    crypto::ChaCha20 cc_poly;
    cc_poly.set_key(key);
    cc_poly.set_nonce(nonce);
    cc_poly.set_counter(0);
    u8 poly_key[32];
    cc_poly.encrypt(poly_key, 32, poly_key);

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
    if (aad_rem) mac.update(zero_pad, 16 - aad_rem);
    mac.update(ciphertext, encrypted_len);
    std::size_t ct_rem = encrypted_len % 16;
    if (ct_rem) mac.update(zero_pad, 16 - ct_rem);
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
    if (diff != 0) { content_type = 0; return {}; }

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

std::vector<u8> TLSConnection::aead_encrypt(const u8 key[32], const u8 iv[12], u64 seq,
                                              const u8* plaintext, u32 pt_len,
                                              u8 content_type) {
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

std::vector<u8> TLSConnection::aead_decrypt(const u8 key[32], const u8 iv[12], u64 seq,
                                              const u8* ciphertext, u32 ct_len,
                                              u8& content_type) {
    if (cipher_suite_ == 0x1303)
        return chacha20_poly1305_decrypt_impl(key, iv, seq, ciphertext, ct_len, content_type);
    if (ct_len < 16) {
        content_type = 0;
        return {};
    }

    u32 encrypted_len = ct_len - 16;
    const u8* tag = ciphertext + encrypted_len;

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

std::vector<u8> TLSConnection::build_client_hello(const std::string& hostname) {
    // Generate keypair
    crypto::X25519::generate_keypair(client_priv_, client_pub_);

    // Build ClientHello body
    std::vector<u8> ch;

    // Legacy version: 0x0303
    ch.push_back(0x03);
    ch.push_back(0x03);

    // Random: 32 bytes
    u8 random[32];
    BCryptGenRandom(nullptr, random, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    ch.insert(ch.end(), random, random + 32);

    // Session ID: empty
    ch.push_back(0x00);

    // Cipher suites:
    // TLS_AES_128_GCM_SHA256 (0x1301), TLS_CHACHA20_POLY1305_SHA256 (0x1303)
    u16 cs_count = 2;
    ch.push_back(static_cast<u8>((cs_count * 2) >> 8));
    ch.push_back(static_cast<u8>((cs_count * 2)));
    ch.push_back(0x13); ch.push_back(0x01); // TLS_AES_128_GCM_SHA256
    ch.push_back(0x13); ch.push_back(0x03); // TLS_CHACHA20_POLY1305_SHA256

    // Compression: null only
    ch.push_back(0x01);
    ch.push_back(0x00);

    // Extensions
    std::vector<u8> exts;

    // Extension: supported_versions (0x002b)
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x2b);
        std::vector<u8> data;
        data.push_back(0x02); // length of version list
        data.push_back(0x03); data.push_back(0x04); // TLS 1.3 = 0x0304
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extension: key_share (0x0033)
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x33);
        std::vector<u8> entry;
        entry.push_back(0x00); entry.push_back(0x1d); // x25519 group
        entry.push_back(0x00); entry.push_back(0x20); // key length = 32
        entry.insert(entry.end(), client_pub_, client_pub_ + 32);
        // KeyShareClientHello: client_shares<0..2^16-1>
        std::vector<u8> data;
        data.push_back(static_cast<u8>((entry.size() >> 8) & 0xFF));
        data.push_back(static_cast<u8>(entry.size() & 0xFF));
        data.insert(data.end(), entry.begin(), entry.end());
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extension: signature_algorithms (0x000d)
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x0d);
        std::vector<u8> data;
        u8 algs[] = {0x08, 0x04, 0x04, 0x03, 0x08, 0x09, 0x08, 0x0a}; // rsa_pss*, ecdsa*
        u16 alg_len = sizeof(algs);
        data.push_back(static_cast<u8>((alg_len >> 8) & 0xFF));
        data.push_back(static_cast<u8>(alg_len & 0xFF));
        data.insert(data.end(), algs, algs + alg_len);
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extension: supported_groups (0x000a)
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x0a);
        std::vector<u8> data;
        u8 groups[] = {0x00, 0x1d, 0x00, 0x17}; // x25519, secp256r1
        u16 grp_len = sizeof(groups);
        data.push_back(static_cast<u8>((grp_len >> 8) & 0xFF));
        data.push_back(static_cast<u8>(grp_len & 0xFF));
        data.insert(data.end(), groups, groups + grp_len);
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extension: ALPN (0x0010) — advertise h2 (HTTP/2) and http/1.1
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x10);
        std::vector<u8> data;
        std::vector<u8> proto;
        proto.push_back(0x02); // "h2" length
        proto.insert(proto.end(), (const u8*)"h2", (const u8*)"h2" + 2);
        proto.push_back(0x08); // "http/1.1" length
        proto.insert(proto.end(), (const u8*)"http/1.1", (const u8*)"http/1.1" + 8);
        u16 proto_len = static_cast<u16>(proto.size());
        data.push_back(static_cast<u8>((proto_len >> 8) & 0xFF));
        data.push_back(static_cast<u8>(proto_len & 0xFF));
        data.insert(data.end(), proto.begin(), proto.end());
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extension: SNI (0x0000) - for hostname
    {
        std::vector<u8> ext;
        ext.push_back(0x00); ext.push_back(0x00);
        std::vector<u8> data;
        std::vector<u8> sni;
        sni.push_back(0x00); // host_name type
        u16 name_len = static_cast<u16>(hostname.size());
        sni.push_back(static_cast<u8>((name_len >> 8) & 0xFF));
        sni.push_back(static_cast<u8>(name_len & 0xFF));
        sni.insert(sni.end(), hostname.begin(), hostname.end());
        u16 sni_len = static_cast<u16>(sni.size());
        data.push_back(static_cast<u8>((sni_len >> 8) & 0xFF));
        data.push_back(static_cast<u8>(sni_len & 0xFF));
        data.insert(data.end(), sni.begin(), sni.end());
        ext.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        ext.push_back(static_cast<u8>(data.size() & 0xFF));
        ext.insert(ext.end(), data.begin(), data.end());
        exts.insert(exts.end(), ext.begin(), ext.end());
    }

    // Extensions total length (2 bytes before extensions)
    u16 exts_len = static_cast<u16>(exts.size());
    ch.push_back(static_cast<u8>((exts_len >> 8) & 0xFF));
    ch.push_back(static_cast<u8>(exts_len & 0xFF));
    ch.insert(ch.end(), exts.begin(), exts.end());

    return ch;
}

Result<void> TLSConnection::connect(Connection* tcp, const std::string& hostname) {
    reset_state();
    tcp_ = tcp;
    u64 deadline = GetTickCount64() + 15000;

    // Build and send ClientHello
    auto ch_body = build_client_hello(hostname);
    append_handshake_to_transcript(HS_CLIENT_HELLO, ch_body);

    auto ch_msg = make_handshake_msg(HS_CLIENT_HELLO, ch_body);
    auto r = send_raw_record(HANDSHAKE, ch_msg);
    if (r.is_err()) return std::string("send client hello: " + r.unwrap_err());

    // Read ServerHello
    if (GetTickCount64() > deadline) return std::string("handshake timeout");
    auto sh_r = read_raw_record();
    if (sh_r.is_err()) return std::string("read server hello: " + sh_r.unwrap_err());
    auto& sh_data = sh_r.unwrap();

    ParsedHS hs;
    if (!parse_hs_header(sh_data, 0, hs)) return std::string("bad server hello header");
    if (hs.type != HS_SERVER_HELLO) return std::string("expected server hello");

    // Update transcript (the full handshake struct)
    transcript_.insert(transcript_.end(), sh_data.begin(), sh_data.end());
    transcript_hasher_.update(sh_data.data(), sh_data.size());

    // Parse ServerHello
    auto sh_body = std::vector<u8>(sh_data.begin() + 4, sh_data.end());
    std::size_t off = 0;

    // legacy_version (2 bytes)
    if (off + 2 > sh_body.size()) return std::string("truncated SH");
    off += 2;

    // random (32 bytes)
    if (off + 32 > sh_body.size()) return std::string("truncated SH random");
    {
        auto sr = std::vector<u8>(sh_body.begin() + off, sh_body.begin() + off + 32);
        off += 32;
    }

    // session_id (1 byte length + data)
    if (off + 1 > sh_body.size()) return std::string("truncated SH sid");
    u8 sid_len = sh_body[off++];
    if (off + sid_len > sh_body.size()) return std::string("truncated SH sid2");
    off += sid_len;

    // cipher_suite (2 bytes)
    if (off + 2 > sh_body.size()) return std::string("truncated SH cs");
    cipher_suite_ = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
    off += 2;
    if (cipher_suite_ != 0x1301 && cipher_suite_ != 0x1303)
        return std::string("unsupported cipher suite: " + std::to_string(cipher_suite_));

    // compression (1 byte)
    if (off + 1 > sh_body.size()) return std::string("truncated SH comp");
    off++;

    // extensions (2 bytes length + data)
    if (off + 2 > sh_body.size()) return std::string("truncated SH ext");
    u16 sh_exts_len = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
    off += 2;

    if (off + sh_exts_len > sh_body.size()) return std::string("truncated SH exts2");

    // Find key_share extension
    std::vector<u8> server_pub;
    std::size_t ext_off = off;
    std::size_t ext_end = off + sh_exts_len;

    while (ext_off + 4 <= ext_end) {
        u16 ext_type = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
        u16 ext_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
        ext_off += 4;
        if (ext_off + ext_len > ext_end) break;

        if (ext_type == 0x0033 && ext_len >= 4) {
            // key_share entry: group (2), key_len (2), key
            u16 group = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
            u16 key_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
            if (group == 0x001d && key_len == 32 && ext_off + 4 + key_len <= ext_end) {
                server_pub.assign(sh_body.begin() + ext_off + 4,
                                  sh_body.begin() + ext_off + 4 + key_len);
            }
        }
        ext_off += ext_len;
    }

    if (server_pub.size() != 32) return std::string("no key_share in SH");

    // Compute shared secret via X25519
    u8 shared_secret[32];
    crypto::X25519::shared_secret(client_priv_, server_pub.data(), shared_secret);
    std::vector<u8> ss(shared_secret, shared_secret + 32);

    // Derive handshake keys
    derive_handshake_keys(ss);

    // Read remaining handshake records (EncryptedExtensions, Certificate, CertificateVerify, Finished)
    // Each record may contain multiple handshake messages
    int msgs_needed = 4; // EE, Cert, CV, Finished
    while (msgs_needed > 0) {
        if (GetTickCount64() > deadline) return std::string("handshake timeout");
        auto rec_r = read_encrypted_record(server_hs_key_, server_hs_iv_, server_seq_);
        if (rec_r.is_err()) return std::string("read hs record: " + rec_r.unwrap_err());
        auto& rec_data = rec_r.unwrap();
        if (rec_data.empty()) continue;

        // Parse handshake messages in this record
        std::size_t msg_off = 0;
        while (msg_off < rec_data.size() && msgs_needed > 0) {
            ParsedHS phs;
            if (!parse_hs_header(rec_data, msg_off, phs))
                return std::string("bad hs header in encrypted record");

            u32 total_len = 4 + phs.body_len;
            if (msg_off + total_len > rec_data.size())
                return std::string("truncated hs msg");

            auto msg_bytes = std::vector<u8>(rec_data.begin() + msg_off,
                                             rec_data.begin() + msg_off + total_len);

            auto body = std::vector<u8>(rec_data.begin() + msg_off + 4,
                                        rec_data.begin() + msg_off + total_len);

            if (phs.type == HS_ENCRYPTED_EXTENSIONS) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                // Parse for ALPN
                std::size_t eo = 0;
                if (eo + 2 <= body.size()) {
                    u16 ee_exts_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                    eo += 2;
                    std::size_t ee_end = eo + ee_exts_len;
                    while (eo + 4 <= ee_end) {
                        u16 etype = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                        u16 elen = (static_cast<u16>(body[eo + 2]) << 8) | body[eo + 3];
                        eo += 4;
                        if (eo + elen > ee_end) break;
                        if (etype == 0x0010) {
                            // ALPN extension
                            u16 list_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                            if (list_len >= 2 && eo + 2 + list_len <= ee_end) {
                                u8 proto_len = body[eo + 2];
                                if (proto_len > 0 && proto_len <= list_len - 1) {
                                    alpn_.assign(reinterpret_cast<const char*>(body.data() + eo + 3), proto_len);
                                }
                            }
                        }
                        eo += elen;
                    }
                }
                msgs_needed--;
            } else if (phs.type == HS_CERTIFICATE) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                /* SECURITY: Certificate validation skipped. Not for production use. */
                msgs_needed--;
            } else if (phs.type == HS_CERTIFICATE_VERIFY) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                /* SECURITY: CertificateVerify signature verification skipped. */
                msgs_needed--;
            } else if (phs.type == HS_FINISHED) {
                // Verify server finished (compute hash BEFORE adding to transcript)
                auto transcript_hash = compute_transcript_hash();
                auto finished_key = hkdf_expand_label(server_hs_traffic_, "finished", {}, 32);
                auto expected_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
                if (body.size() != expected_verify_data.size() ||
                    std::memcmp(body.data(), expected_verify_data.data(), body.size()) != 0) {
                    return std::string("server finished verification failed");
                }
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                msgs_needed--;
            } else if (phs.type == HS_CLIENT_HELLO || phs.type == HS_SERVER_HELLO) {
                // Should not happen after encrypted
                return std::string("unexpected hs type");
            }

            msg_off += total_len;
        }
    }

    if (msgs_needed != 0) return std::string("missing handshake messages");

    // Send ClientFinished
    auto transcript_hash = compute_transcript_hash();
    auto finished_key = hkdf_expand_label(client_hs_traffic_, "finished", {}, 32);
    auto finished_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
    std::vector<u8> finished_body(finished_verify_data.begin(), finished_verify_data.end());

    // Derive application keys BEFORE adding client Finished to transcript
    derive_application_keys();

    // Append finished to transcript
    append_handshake_to_transcript(HS_FINISHED, finished_body);

    // Send encrypted Finished
    auto r2 = send_encrypted_record(HANDSHAKE, finished_body,
                                     client_hs_key_, client_hs_iv_, client_seq_);
    if (r2.is_err()) return std::string("send finished: " + r2.unwrap_err());

    // Reset sequence numbers for app data
    client_seq_ = 0;
    server_seq_ = 0;

    connected_ = true;
    return {};
}

async::task<bool> TLSConnection::connect_async(Connection* tcp, const std::string& hostname) {
    reset_state();
    tcp_ = tcp;
    u64 deadline = GetTickCount64() + 15000;

    // Build and send ClientHello
    auto ch_body = build_client_hello(hostname);
    append_handshake_to_transcript(HS_CLIENT_HELLO, ch_body);

    auto ch_msg = make_handshake_msg(HS_CLIENT_HELLO, ch_body);
    auto r = co_await send_raw_record_async(HANDSHAKE, ch_msg);
    if (r.is_err()) co_return std::string("send client hello: ") + r.unwrap_err();

    // Read ServerHello
    if (GetTickCount64() > deadline) co_return std::string("handshake timeout");
    auto sh_r = co_await read_raw_record_async();
    if (sh_r.is_err()) co_return std::string("read server hello: ") + sh_r.unwrap_err();
    auto sh_data = sh_r.unwrap();

    ParsedHS hs;
    if (!parse_hs_header(sh_data, 0, hs)) co_return std::string("bad server hello header");
    if (hs.type != HS_SERVER_HELLO) co_return std::string("expected server hello");

    transcript_.insert(transcript_.end(), sh_data.begin(), sh_data.end());
    transcript_hasher_.update(sh_data.data(), sh_data.size());

    auto sh_body = std::vector<u8>(sh_data.begin() + 4, sh_data.end());
    std::size_t off = 0;

    if (off + 2 > sh_body.size()) co_return std::string("truncated SH");
    off += 2;
    if (off + 32 > sh_body.size()) co_return std::string("truncated SH random");
    off += 32;
    if (off + 1 > sh_body.size()) co_return std::string("truncated SH sid");
    u8 sid_len = sh_body[off++];
    if (off + sid_len > sh_body.size()) co_return std::string("truncated SH sid2");
    off += sid_len;
    if (off + 2 > sh_body.size()) co_return std::string("truncated SH cs");
    cipher_suite_ = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
    off += 2;
    if (cipher_suite_ != 0x1301 && cipher_suite_ != 0x1303)
        co_return std::string("unsupported cipher suite: ") + std::to_string(cipher_suite_);
    if (off + 1 > sh_body.size()) co_return std::string("truncated SH comp");
    off++;
    if (off + 2 > sh_body.size()) co_return std::string("truncated SH ext");
    u16 sh_exts_len = (static_cast<u16>(sh_body[off]) << 8) | sh_body[off + 1];
    off += 2;
    if (off + sh_exts_len > sh_body.size()) co_return std::string("truncated SH exts2");

    std::vector<u8> server_pub;
    std::size_t ext_off = off;
    std::size_t ext_end = off + sh_exts_len;
    while (ext_off + 4 <= ext_end) {
        u16 ext_type = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
        u16 ext_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
        ext_off += 4;
        if (ext_off + ext_len > ext_end) break;
        if (ext_type == 0x0033 && ext_len >= 4) {
            u16 group = (static_cast<u16>(sh_body[ext_off]) << 8) | sh_body[ext_off + 1];
            u16 key_len = (static_cast<u16>(sh_body[ext_off + 2]) << 8) | sh_body[ext_off + 3];
            if (group == 0x001d && key_len == 32 && ext_off + 4 + key_len <= ext_end) {
                server_pub.assign(sh_body.begin() + ext_off + 4, sh_body.begin() + ext_off + 4 + key_len);
            }
        }
        ext_off += ext_len;
    }
    if (server_pub.size() != 32) co_return std::string("no key_share in SH");

    u8 shared_secret[32];
    crypto::X25519::shared_secret(client_priv_, server_pub.data(), shared_secret);
    std::vector<u8> ss(shared_secret, shared_secret + 32);
    derive_handshake_keys(ss);

    int msgs_needed = 4;
    while (msgs_needed > 0) {
        if (GetTickCount64() > deadline) co_return std::string("handshake timeout");
        auto rec_r = co_await read_encrypted_record_async(server_hs_key_, server_hs_iv_, server_seq_);
        if (rec_r.is_err()) co_return std::string("read hs: ") + rec_r.unwrap_err();
        auto rec_data = rec_r.unwrap();
        if (rec_data.empty()) continue;

        std::size_t msg_off = 0;
        while (msg_off < rec_data.size() && msgs_needed > 0) {
            ParsedHS phs;
            if (!parse_hs_header(rec_data, msg_off, phs))
                co_return std::string("bad hs header in encrypted record");

            u32 total_len = 4 + phs.body_len;
            if (msg_off + total_len > rec_data.size())
                co_return std::string("truncated hs msg");

            auto msg_bytes = std::vector<u8>(rec_data.begin() + msg_off, rec_data.begin() + msg_off + total_len);
            auto body = std::vector<u8>(rec_data.begin() + msg_off + 4, rec_data.begin() + msg_off + total_len);

            if (phs.type == HS_ENCRYPTED_EXTENSIONS) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                std::size_t eo = 0;
                if (eo + 2 <= body.size()) {
                    u16 ee_exts_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                    eo += 2;
                    std::size_t ee_end = eo + ee_exts_len;
                    while (eo + 4 <= ee_end) {
                        u16 etype = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                        u16 elen = (static_cast<u16>(body[eo + 2]) << 8) | body[eo + 3];
                        eo += 4;
                        if (eo + elen > ee_end) break;
                        if (etype == 0x0010) {
                            u16 list_len = (static_cast<u16>(body[eo]) << 8) | body[eo + 1];
                            if (list_len >= 2 && eo + 2 + list_len <= ee_end) {
                                u8 proto_len = body[eo + 2];
                                if (proto_len > 0 && proto_len <= list_len - 1) {
                                    alpn_.assign(reinterpret_cast<const char*>(body.data() + eo + 3), proto_len);
                                }
                            }
                        }
                        eo += elen;
                    }
                }
                msgs_needed--;
            } else if (phs.type == HS_CERTIFICATE) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                msgs_needed--;
            } else if (phs.type == HS_CERTIFICATE_VERIFY) {
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                msgs_needed--;
            } else if (phs.type == HS_FINISHED) {
                auto transcript_hash = compute_transcript_hash();
                auto finished_key = hkdf_expand_label(server_hs_traffic_, "finished", {}, 32);
                auto expected_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
                if (body.size() != expected_verify_data.size() ||
                    std::memcmp(body.data(), expected_verify_data.data(), body.size()) != 0) {
                    co_return std::string("server finished verification failed");
                }
                transcript_.insert(transcript_.end(), msg_bytes.begin(), msg_bytes.end());
                transcript_hasher_.update(msg_bytes.data(), msg_bytes.size());
                msgs_needed--;
            } else if (phs.type == HS_CLIENT_HELLO || phs.type == HS_SERVER_HELLO) {
                co_return std::string("unexpected hs type");
            }
            msg_off += total_len;
        }
    }

    if (msgs_needed != 0) co_return std::string("missing handshake messages");

    auto transcript_hash = compute_transcript_hash();
    auto finished_key = hkdf_expand_label(client_hs_traffic_, "finished", {}, 32);
    auto finished_verify_data = crypto::hmac_sha256(finished_key, transcript_hash);
    std::vector<u8> finished_body(finished_verify_data.begin(), finished_verify_data.end());

    derive_application_keys();
    append_handshake_to_transcript(HS_FINISHED, finished_body);

    auto r2 = co_await send_encrypted_record_async(HANDSHAKE, finished_body,
                                                    client_hs_key_, client_hs_iv_, client_seq_);
    if (r2.is_err()) co_return std::string("send finished: ") + r2.unwrap_err();

    client_seq_ = 0;
    server_seq_ = 0;
    connected_ = true;
    co_return true;
}

Result<u32> TLSConnection::send(const u8* data, u32 len) {
    if (!is_connected()) return std::string("not connected");
    auto r = send_encrypted_record(APPLICATION_DATA,
                                    std::vector<u8>(data, data + len),
                                    client_app_key_, client_app_iv_, client_seq_);
    if (r.is_err()) return std::string("send: " + r.unwrap_err());
    return len;
}

Result<void> TLSConnection::send_all(const u8* data, u32 len) {
    auto r = send(data, len);
    if (r.is_err()) return std::string("send_all: " + r.unwrap_err());
    return {};
}

Result<u32> TLSConnection::receive(u8* buf, u32 len) {
    if (!is_connected()) return std::string("not connected");
    auto r = read_encrypted_record(server_app_key_, server_app_iv_, server_seq_);
    if (r.is_err()) return std::string("receive: " + r.unwrap_err());
    auto& data = r.unwrap();
    u32 copy_len = static_cast<u32>(data.size());
    if (copy_len > len) copy_len = len;
    std::memcpy(buf, data.data(), copy_len);
    return copy_len;
}

Result<std::vector<u8>> TLSConnection::receive_all(u32 max_size) {
    if (!is_connected()) return std::string("not connected");
    std::vector<u8> result;
    u32 cap = max_size > 0 ? max_size : (1024u * 1024u);
    while (result.size() < cap) {
        auto r = read_encrypted_record(server_app_key_, server_app_iv_, server_seq_);
        if (r.is_err()) {
            if (result.empty())
                return std::string("receive_all: " + r.unwrap_err());
            break;
        }
        auto& data = r.unwrap();
        if (data.empty()) break;
        result.insert(result.end(), data.begin(), data.end());
    }
    return result;
}

async::task<bool> TLSConnection::send_all_async(const u8* data, u32 len) {
    if (!is_connected()) co_return std::string("not connected");
    auto r = co_await send_encrypted_record_async(APPLICATION_DATA,
                                                    std::vector<u8>(data, data + len),
                                                    client_app_key_, client_app_iv_, client_seq_);
    co_return r;
}

async::task<u32> TLSConnection::receive_async(u8* buf, u32 len) {
    if (!is_connected()) co_return std::string("not connected");
    auto r = co_await read_encrypted_record_async(server_app_key_, server_app_iv_, server_seq_);
    if (r.is_err()) co_return std::string("receive: ") + r.unwrap_err();
    auto data = r.unwrap();
    u32 copy_len = static_cast<u32>(data.size());
    if (copy_len > len) copy_len = len;
    std::memcpy(buf, data.data(), copy_len);
    co_return copy_len;
}

} // namespace browser::net::tls

