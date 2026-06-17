#include "connection.hpp"

#include <cstring>

namespace browser::net::tls {

    TLSConnection::TLSConnection() = default;
    TLSConnection::~TLSConnection() {
        close();
    }

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
        peer_certs_.clear();
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

    Result<u32> TLSConnection::send(const u8 *data, u32 len) {
        if (!is_connected())
            return std::string("not connected");
        auto r = send_encrypted_record(
            APPLICATION_DATA, std::vector<u8>(data, data + len), client_app_key_, client_app_iv_, client_seq_);
        if (r.is_err())
            return std::string("send: " + r.unwrap_err());
        return len;
    }

    Result<void> TLSConnection::send_all(const u8 *data, u32 len) {
        auto r = send(data, len);
        if (r.is_err())
            return std::string("send_all: " + r.unwrap_err());
        return {};
    }

    Result<u32> TLSConnection::receive(u8 *buf, u32 len) {
        if (!is_connected())
            return std::string("not connected");
        auto r = read_encrypted_record(server_app_key_, server_app_iv_, server_seq_);
        if (r.is_err())
            return std::string("receive: " + r.unwrap_err());
        auto &data = r.unwrap();
        u32 copy_len = static_cast<u32>(data.size());
        if (copy_len > len)
            copy_len = len;
        std::memcpy(buf, data.data(), copy_len);
        return copy_len;
    }

    Result<std::vector<u8>> TLSConnection::receive_all(u32 max_size) {
        if (!is_connected())
            return std::string("not connected");
        std::vector<u8> result;
        u32 cap = max_size > 0 ? max_size : (1024u * 1024u);
        while (result.size() < cap) {
            auto r = read_encrypted_record(server_app_key_, server_app_iv_, server_seq_);
            if (r.is_err()) {
                if (result.empty())
                    return std::string("receive_all: " + r.unwrap_err());
                break;
            }
            auto &data = r.unwrap();
            if (data.empty())
                break;
            result.insert(result.end(), data.begin(), data.end());
        }
        return result;
    }

    async::task<bool> TLSConnection::send_all_async(const u8 *data, u32 len) {
        if (!is_connected())
            co_return std::string("not connected");
        auto r = co_await send_encrypted_record_async(
            APPLICATION_DATA, std::vector<u8>(data, data + len), client_app_key_, client_app_iv_, client_seq_);
        co_return r;
    }

    async::task<u32> TLSConnection::receive_async(u8 *buf, u32 len) {
        if (!is_connected())
            co_return std::string("not connected");
        auto r = co_await read_encrypted_record_async(server_app_key_, server_app_iv_, server_seq_);
        if (r.is_err())
            co_return std::string("receive: ") + r.unwrap_err();
        auto data = r.unwrap();
        u32 copy_len = static_cast<u32>(data.size());
        if (copy_len > len)
            copy_len = len;
        std::memcpy(buf, data.data(), copy_len);
        co_return copy_len;
    }

}  // namespace browser::net::tls
