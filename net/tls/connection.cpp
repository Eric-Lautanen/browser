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
        recv_pending_.clear();
        recv_pending_pos_ = 0;
        recv_eof_ = false;
        tcp_closed_ = false;
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

    Result<bool> TLSConnection::fill_recv_pending() {
        for (;;) {
            if (recv_pending_pos_ < recv_pending_.size())
                return true;
            recv_pending_.clear();
            recv_pending_pos_ = 0;
            if (recv_eof_)
                return false;

            u8 inner = 0;
            auto r = read_encrypted_record(server_app_key_, server_app_iv_, server_seq_, &inner);
            if (r.is_err())
                return std::string("receive: " + r.unwrap_err());
            auto payload = std::move(r.unwrap());
            if (payload.empty())
                continue;  // zero-length fragment or skipped record

            if (inner == APPLICATION_DATA) {
                recv_pending_ = std::move(payload);
                recv_pending_pos_ = 0;
                return true;
            }
            if (inner == HANDSHAKE)
                continue;  // post-handshake message (e.g. NewSessionTicket): not app data
            if (inner == ALERT) {
                // TLS 1.3: alerts arrive as [level, description] payloads.
                if (payload.size() >= 2 && payload[1] == 0) {  // close_notify
                    recv_eof_ = true;
                    return false;
                }
                if (payload.size() >= 2)
                    return std::string("tls alert: description " + std::to_string(payload[1]));
                return std::string("malformed tls alert record");
            }
            return std::string("unexpected inner content type " + std::to_string(inner));
        }
    }

    async::task<bool> TLSConnection::fill_recv_pending_async() {
        for (;;) {
            if (recv_pending_pos_ < recv_pending_.size())
                co_return true;
            recv_pending_.clear();
            recv_pending_pos_ = 0;
            if (recv_eof_)
                co_return false;

            u8 inner = 0;
            auto r = co_await read_encrypted_record_async(server_app_key_, server_app_iv_, server_seq_, &inner);
            if (r.is_err())
                co_return std::string("receive: ") + r.unwrap_err();
            auto payload = r.unwrap();
            if (payload.empty())
                continue;

            if (inner == APPLICATION_DATA) {
                recv_pending_ = std::move(payload);
                recv_pending_pos_ = 0;
                co_return true;
            }
            if (inner == HANDSHAKE)
                continue;
            if (inner == ALERT) {
                if (payload.size() >= 2 && payload[1] == 0) {
                    recv_eof_ = true;
                    co_return false;
                }
                if (payload.size() >= 2)
                    co_return std::string("tls alert: description ") + std::to_string(payload[1]);
                co_return std::string("malformed tls alert record");
            }
            co_return std::string("unexpected inner content type ") + std::to_string(inner);
        }
    }

    Result<u32> TLSConnection::receive(u8 *buf, u32 len) {
        if (!is_connected())
            return std::string("not connected");
        auto f = fill_recv_pending();
        if (f.is_err())
            return f.unwrap_err();
        if (!f.unwrap())
            return 0u;  // clean EOF: nothing buffered, close_notify received
        u32 avail = static_cast<u32>(recv_pending_.size() - recv_pending_pos_);
        u32 n = avail < len ? avail : len;
        std::memcpy(buf, recv_pending_.data() + recv_pending_pos_, n);
        recv_pending_pos_ += n;
        return n;
    }

    Result<std::vector<u8>> TLSConnection::receive_all(u32 max_size) {
        if (!is_connected())
            return std::string("not connected");
        std::vector<u8> result;
        u32 cap = max_size > 0 ? max_size : (1024u * 1024u);
        while (result.size() < cap) {
            // Drain whatever is already decrypted.
            while (result.size() < cap && recv_pending_pos_ < recv_pending_.size()) {
                result.push_back(recv_pending_[recv_pending_pos_++]);
            }
            if (result.size() >= cap)
                break;
            auto f = fill_recv_pending();
            if (f.is_err()) {
                // N-C2: only an abrupt TCP close may end delivery early (with the
                // partial data); protocol/decrypt errors propagate. The HTTP layer
                // validates completeness independently.
                if (tcp_closed_)
                    break;
                return f.unwrap_err();
            }
            if (!f.unwrap())  // close_notify
                break;
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
        auto f = co_await fill_recv_pending_async();
        if (f.is_err())
            co_return f.unwrap_err();
        if (!f.unwrap())
            co_return 0u;  // clean EOF
        u32 avail = static_cast<u32>(recv_pending_.size() - recv_pending_pos_);
        u32 n = avail < len ? avail : len;
        std::memcpy(buf, recv_pending_.data() + recv_pending_pos_, n);
        recv_pending_pos_ += n;
        co_return n;
    }

}  // namespace browser::net::tls
