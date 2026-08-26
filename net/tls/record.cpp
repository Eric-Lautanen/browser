#include "connection.hpp"

#include <cstring>

namespace browser::net::tls {

    static std::vector<u8> make_record(u8 type, const std::vector<u8> &data) {
        if (data.size() > 0xFFFF)
            return {};  // caller must chunk; u16 length field would wrap
        std::vector<u8> record;
        record.push_back(type);
        record.push_back(0x03);
        record.push_back(0x03);
        record.push_back(static_cast<u8>((data.size() >> 8) & 0xFF));
        record.push_back(static_cast<u8>(data.size() & 0xFF));
        record.insert(record.end(), data.begin(), data.end());
        return record;
    }

    Result<void> TLSConnection::send_raw_record(u8 type, const std::vector<u8> &data) {
        if (!tcp_)
            return std::string("no connection");
        auto record = make_record(type, data);
        if (record.empty())
            return std::string("record payload too large");
        return tcp_->send_all(record.data(), static_cast<u32>(record.size()));
    }

    async::task<bool> TLSConnection::send_raw_record_async(u8 type, const std::vector<u8> &data) {
        if (!tcp_)
            co_return std::string("no connection");
        auto record = make_record(type, data);
        if (record.empty())
            co_return std::string("record payload too large");
        auto r = co_await tcp_->send_all_async(record.data(), static_cast<u32>(record.size()));
        co_return r;
    }

    Result<std::vector<u8>> TLSConnection::read_raw_record(u8 *out_type) {
        u8 header[5];
        {
            u32 hgot = 0;
            while (hgot < 5) {
                auto r = tcp_->receive(header + hgot, 5 - hgot);
                if (r.is_err())
                    return std::string("read header: " + r.unwrap_err());
                u32 n = r.unwrap();
                if (n == 0) {
                    tcp_closed_ = true;
                    return std::string("connection closed during header read");
                }
                hgot += n;
            }
        }

        u8 type = header[0];
        u16 version = (static_cast<u16>(header[1]) << 8) | header[2];
        u16 length = (static_cast<u16>(header[3]) << 8) | header[4];
        if (version != 0x0303)
            return std::string("bad record version");

        if (out_type)
            *out_type = type;

        if (length > kMaxCiphertextRecord)
            return std::string("record too large: " + std::to_string(length));

        if (length == 0)
            return std::vector<u8>();

        std::vector<u8> data(length);
        u32 got = 0;
        while (got < length) {
            auto rr = tcp_->receive(data.data() + got, length - got);
            if (rr.is_err())
                return std::string("read data: " + rr.unwrap_err());
            u32 n = rr.unwrap();
            if (n == 0) {
                tcp_closed_ = true;
                return std::string("connection closed during data read");
            }
            got += n;
        }

        return data;
    }

    async::task<std::vector<u8>> TLSConnection::read_raw_record_async(u8 *out_type) {
        u8 header[5];
        {
            u32 hgot = 0;
            while (hgot < 5) {
                auto r = co_await tcp_->receive_async(header + hgot, 5 - hgot);
                if (r.is_err())
                    co_return std::string("read header: ") + r.unwrap_err();
                u32 n = r.unwrap();
                if (n == 0) {
                    tcp_closed_ = true;
                    co_return std::string("connection closed during header read");
                }
                hgot += n;
            }
        }

        u8 type = header[0];
        u16 version = (static_cast<u16>(header[1]) << 8) | header[2];
        u16 length = (static_cast<u16>(header[3]) << 8) | header[4];
        if (version != 0x0303)
            co_return std::string("bad record version");

        if (out_type)
            *out_type = type;

        if (length > kMaxCiphertextRecord)
            co_return std::string("record too large: ") + std::to_string(length);

        if (length == 0)
            co_return std::vector<u8>();

        std::vector<u8> data(length);
        u32 got = 0;
        while (got < length) {
            auto rr = co_await tcp_->receive_async(data.data() + got, length - got);
            if (rr.is_err())
                co_return std::string("read data: ") + rr.unwrap_err();
            u32 n = rr.unwrap();
            if (n == 0) {
                tcp_closed_ = true;
                co_return std::string("connection closed during data read");
            }
            got += n;
        }

        co_return data;
    }

    Result<void> TLSConnection::send_encrypted_record(
        u8 inner_type, const std::vector<u8> &data, const u8 key[32], const u8 iv[12], u64 &seq) {
        // N-C6: chunk large payloads into <=16KiB plaintext fragments so the
        // u16 record length never wraps and each record stays within the TLS
        // maximum ciphertext size.
        u32 offset = 0;
        do {
            u32 chunk = data.size() - offset;
            if (chunk > kMaxPlaintextFragment)
                chunk = kMaxPlaintextFragment;
            std::vector<u8> piece(data.begin() + offset, data.begin() + offset + chunk);
            auto ct = aead_encrypt(key, iv, seq, piece.data(), static_cast<u32>(piece.size()), inner_type);
            seq++;
            offset += chunk;
            auto r = send_raw_record(APPLICATION_DATA, ct);
            if (r.is_err())
                return r;
        } while (offset < data.size());
        return {};
    }

    Result<std::vector<u8>> TLSConnection::read_encrypted_record(const u8 key[32],
                                                                 const u8 iv[12],
                                                                 u64 &seq,
                                                                 u8 *out_inner) {
        for (;;) {
            u8 type = 0;
            auto r = read_raw_record(&type);
            if (r.is_err())
                return std::string("read encrypted: " + r.unwrap_err());
            auto &ct = r.unwrap();
            if (ct.empty())
                continue;  // zero-length record: read again
            if (type == CHANGE_CIPHER_SPEC)
                continue;  // legacy compat record: ignore

            if (type == ALERT)
                return std::string("plaintext tls alert record");

            if (type != APPLICATION_DATA)
                return std::string("unexpected record type " + std::to_string(type));

            u8 inner_type = 0;
            auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
            seq++;
            if (inner_type == 0 && pt.empty())
                return std::string("decryption failed");
            if (out_inner)
                *out_inner = inner_type;
            return pt;
        }
    }

    async::task<bool> TLSConnection::send_encrypted_record_async(
        u8 inner_type, const std::vector<u8> &data, const u8 key[32], const u8 iv[12], u64 &seq) {
        u32 offset = 0;
        do {
            u32 chunk = data.size() - offset;
            if (chunk > kMaxPlaintextFragment)
                chunk = kMaxPlaintextFragment;
            std::vector<u8> piece(data.begin() + offset, data.begin() + offset + chunk);
            auto ct = aead_encrypt(key, iv, seq, piece.data(), static_cast<u32>(piece.size()), inner_type);
            seq++;
            offset += chunk;
            auto r = co_await send_raw_record_async(APPLICATION_DATA, ct);
            if (r.is_err())
                co_return r.unwrap_err();
        } while (offset < data.size());
        co_return true;
    }

    async::task<std::vector<u8>> TLSConnection::read_encrypted_record_async(const u8 key[32],
                                                                            const u8 iv[12],
                                                                            u64 &seq,
                                                                            u8 *out_inner) {
        for (;;) {
            u8 type = 0;
            auto r = co_await read_raw_record_async(&type);
            if (r.is_err())
                co_return std::string("read encrypted: ") + r.unwrap_err();
            auto ct = r.unwrap();
            if (ct.empty())
                continue;
            if (type == CHANGE_CIPHER_SPEC)
                continue;

            if (type == ALERT)
                co_return std::string("plaintext tls alert record");

            if (type != APPLICATION_DATA)
                co_return std::string("unexpected record type ") + std::to_string(type);

            u8 inner_type = 0;
            auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
            seq++;
            if (inner_type == 0 && pt.empty())
                co_return std::string("decryption failed");
            if (out_inner)
                *out_inner = inner_type;
            co_return pt;
        }
    }

}  // namespace browser::net::tls
