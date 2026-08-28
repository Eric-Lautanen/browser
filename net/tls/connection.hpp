#pragma once
#include "../../async/task.hpp"
#include "../../core/utility.hpp"
#include "../connection.hpp"
#include "../crypto/aes.hpp"
#include "../crypto/sha.hpp"

#include <memory>
#include <string>
#include <vector>

namespace browser::net::tls {

    enum ContentType : u8 { CHANGE_CIPHER_SPEC = 20, ALERT = 21, HANDSHAKE = 22, APPLICATION_DATA = 23 };

    // TLS maximum plaintext fragment per record (RFC 8446 §5.1). Larger
    // payloads are split into multiple records (audit N-C6).
    inline constexpr u32 kMaxPlaintextFragment = 16384;
    // Maximum ciphertext record we accept on receive: 2^14 + 256.
    inline constexpr u32 kMaxCiphertextRecord = 16640;
    // Safety cap for handshake-message reassembly across records (N-C10).
    inline constexpr u32 kMaxHandshakeBuffer = 1024u * 1024u;

    enum HandshakeType : u8 {
        HS_CLIENT_HELLO = 1,
        HS_SERVER_HELLO = 2,
        HS_ENCRYPTED_EXTENSIONS = 8,
        HS_CERTIFICATE = 11,
        HS_CERTIFICATE_VERIFY = 15,
        HS_FINISHED = 20
    };

    // Parses a TLS 1.3 Certificate message body into its DER certificate chain.
    // Fails closed: any length field exceeding the message body is an error, never
    // a silent truncation (N-S3).
    Result<std::vector<std::vector<u8>>> parse_certificate_message(const std::vector<u8> &body);

    class TLSConnection {
    public:
        TLSConnection();
        ~TLSConnection();

        // Sync methods
        Result<void> connect(Connection *tcp, const std::string &hostname);
        Result<u32> send(const u8 *data, u32 len);
        Result<void> send_all(const u8 *data, u32 len);
        Result<u32> receive(u8 *buf, u32 len);
        Result<std::vector<u8>> receive_all(u32 max_size = 0);
        void close();
        bool is_connected() const;
        std::string negotiated_alpn() const;

        // Async methods
        async::task<bool> connect_async(Connection *tcp, const std::string &hostname);
        async::task<bool> send_all_async(const u8 *data, u32 len);
        async::task<u32> receive_async(u8 *buf, u32 len);

    private:
        Connection *tcp_ = nullptr;
        bool connected_ = false;
        bool app_keys_set_ = false;
        std::string alpn_;

        // N-C2: plaintext left over from a decrypted record whose payload
        // exceeded the caller's buffer. Delivered before the next record is read.
        std::vector<u8> recv_pending_;
        std::size_t recv_pending_pos_ = 0;
        // Set when close_notify was received (clean EOF) or the TCP peer closed.
        bool recv_eof_ = false;
        // Set by the record layer when the TCP peer closed underneath us; lets
        // receive_all deliver a partial body instead of masking a hard error.
        bool tcp_closed_ = false;

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

        // Drains buffered plaintext / reads encrypted records until application
        // data is available in recv_pending_ or the stream ends.
        // Sync: Result<bool> — true = data available, false = clean EOF (close_notify).
        // Async: task carries Result<bool> with the same contract.
        // Errors from decryption/protocol violations propagate as errors; an
        // abrupt TCP close sets tcp_closed_ and is reported by receive_all.
        Result<bool> fill_recv_pending();
        async::task<bool> fill_recv_pending_async();  // ok+true=data, ok+false=eof, err=error

        // Sync record layer
        Result<void> send_raw_record(u8 type, const std::vector<u8> &data);
        Result<std::vector<u8>> read_raw_record(u8 *out_type = nullptr);

        // Async record layer
        async::task<bool> send_raw_record_async(u8 type, const std::vector<u8> &data);
        async::task<std::vector<u8>> read_raw_record_async(u8 *out_type = nullptr);

        // Encrypted record layer
        Result<void> send_encrypted_record(
            u8 inner_type, const std::vector<u8> &data, const u8 key[32], const u8 iv[12], u64 &seq);
        // Reads one encrypted record; on success returns its decrypted payload
        // and (when non-null) stores the INNER content type. An empty payload
        // means "nothing to deliver, read again" (zero-length fragment,
        // change-cipher-spec or skipped post-handshake content).
        Result<std::vector<u8>> read_encrypted_record(const u8 key[32],
                                                      const u8 iv[12],
                                                      u64 &seq,
                                                      u8 *out_inner = nullptr);

        async::task<bool> send_encrypted_record_async(
            u8 inner_type, const std::vector<u8> &data, const u8 key[32], const u8 iv[12], u64 &seq);
        async::task<std::vector<u8>> read_encrypted_record_async(const u8 key[32],
                                                                 const u8 iv[12],
                                                                 u64 &seq,
                                                                 u8 *out_inner = nullptr);

        // Shared handshake-message processing used by both connect() paths.
        // Parses the ServerHello body, derives handshake keys; returns server key share.
        Result<std::vector<u8>> process_server_hello(const std::vector<u8> &sh_data);

        // Handles one post-ServerHello handshake message: transcript update, cert parsing,
        // Finished verification. Decrements msgs_needed when an expected message was consumed.
        Result<void> process_hs_message(u8 type,
                                        const std::vector<u8> &msg_bytes,
                                        const std::vector<u8> &body,
                                        int &msgs_needed);

        void append_handshake_to_transcript(u8 type, const std::vector<u8> &body);
        std::vector<u8> compute_transcript_hash() const;

        std::vector<u8> hkdf_expand_label(const std::vector<u8> &secret,
                                          const std::string &label,
                                          const std::vector<u8> &context,
                                          u32 length);
        void derive_handshake_keys(const std::vector<u8> &shared_secret);
        void derive_application_keys();

        std::vector<u8> aead_encrypt(
            const u8 key[32], const u8 iv[12], u64 seq, const u8 *plaintext, u32 pt_len, u8 content_type);
        std::vector<u8> aead_decrypt(
            const u8 key[32], const u8 iv[12], u64 seq, const u8 *ciphertext, u32 ct_len, u8 &content_type);

        std::vector<u8> build_client_hello(const std::string &hostname);

        std::vector<std::vector<u8>> peer_certs_;
    };

}  // namespace browser::net::tls
