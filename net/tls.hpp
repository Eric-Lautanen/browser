#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "connection.hpp"
#include "crypto/sha.hpp"
#include "crypto/aes.hpp"
#include <vector>
#include <string>
#include <memory>

namespace browser::net::tls {

enum ContentType : u8 {
    CHANGE_CIPHER_SPEC = 20,
    ALERT = 21,
    HANDSHAKE = 22,
    APPLICATION_DATA = 23
};

enum HandshakeType : u8 {
    HS_CLIENT_HELLO = 1,
    HS_SERVER_HELLO = 2,
    HS_ENCRYPTED_EXTENSIONS = 8,
    HS_CERTIFICATE = 11,
    HS_CERTIFICATE_VERIFY = 15,
    HS_FINISHED = 20
};

class TLSConnection {
public:
    TLSConnection();
    ~TLSConnection();

    // Sync methods
    Result<void> connect(Connection* tcp, const std::string& hostname);
    Result<u32> send(const u8* data, u32 len);
    Result<void> send_all(const u8* data, u32 len);
    Result<u32> receive(u8* buf, u32 len);
    Result<std::vector<u8>> receive_all(u32 max_size = 0);
    void close();
    bool is_connected() const;
    std::string negotiated_alpn() const;

    // Async methods
    async::task<bool> connect_async(Connection* tcp, const std::string& hostname);
    async::task<bool> send_all_async(const u8* data, u32 len);
    async::task<u32> receive_async(u8* buf, u32 len);

private:
    Connection* tcp_ = nullptr;
    bool connected_ = false;
    bool app_keys_set_ = false;
    std::string alpn_;

    u64 client_seq_ = 0;
    u64 server_seq_ = 0;
    u16 cipher_suite_ = 0;

    std::vector<u8> transcript_;
    crypto::SHA256 transcript_hasher_;

    u8 server_hs_key_[32] = {};
    u8 server_hs_iv_[12] = {};
    u8 client_hs_key_[32] = {};
    u8 client_hs_iv_[12] = {};
    u8 server_app_key_[32] = {};
    u8 server_app_iv_[12] = {};
    u8 client_app_key_[32] = {};
    u8 client_app_iv_[12] = {};

    u8 handshake_secret_[32] = {};
    std::vector<u8> server_hs_traffic_;
    std::vector<u8> client_hs_traffic_;
    std::vector<u8> server_app_traffic_;
    std::vector<u8> client_app_traffic_;

    u8 client_priv_[32];
    u8 client_pub_[32];

    crypto::AES aes_encrypt_;
    crypto::AES aes_decrypt_;

    void reset_state();

    // Sync record layer
    Result<void> send_raw_record(u8 type, const std::vector<u8>& data);
    Result<std::vector<u8>> read_raw_record(u8* out_type = nullptr);

    // Async record layer
    async::task<bool> send_raw_record_async(u8 type, const std::vector<u8>& data);
    async::task<std::vector<u8>> read_raw_record_async(u8* out_type = nullptr);

    // Encrypted record layer
    Result<void> send_encrypted_record(u8 inner_type, const std::vector<u8>& data,
                                       const u8 key[32], const u8 iv[12], u64& seq);
    Result<std::vector<u8>> read_encrypted_record(const u8 key[32], const u8 iv[12], u64& seq);

    async::task<bool> send_encrypted_record_async(u8 inner_type, const std::vector<u8>& data,
                                                   const u8 key[32], const u8 iv[12], u64& seq);
    async::task<std::vector<u8>> read_encrypted_record_async(const u8 key[32], const u8 iv[12], u64& seq);

    void append_handshake_to_transcript(u8 type, const std::vector<u8>& body);
    std::vector<u8> compute_transcript_hash() const;

    std::vector<u8> hkdf_expand_label(const std::vector<u8>& secret,
                                       const std::string& label,
                                       const std::vector<u8>& context,
                                       u32 length);
    void derive_handshake_keys(const std::vector<u8>& shared_secret);
    void derive_application_keys();

    std::vector<u8> aead_encrypt(const u8 key[32], const u8 iv[12], u64 seq,
                                  const u8* plaintext, u32 pt_len,
                                  u8 content_type);
    std::vector<u8> aead_decrypt(const u8 key[32], const u8 iv[12], u64 seq,
                                  const u8* ciphertext, u32 ct_len,
                                  u8& content_type);

    std::vector<u8> build_client_hello(const std::string& hostname);
};

} // namespace browser::net::tls

