#include "connection.hpp"

#include <cstring>

namespace browser::net::tls {

    static std::vector<u8> make_record(u8 type, const std::vector<u8> &data) {
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
        return tcp_->send_all(record.data(), static_cast<u32>(record.size()));
    }

    async::task<bool> TLSConnection::send_raw_record_async(u8 type, const std::vector<u8> &data) {
        if (!tcp_)
            co_return std::string("no connection");
        auto record = make_record(type, data);
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
                if (n == 0)
                    return std::string("connection closed during header read");
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

        if (length > 16640)
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
            if (n == 0)
                return std::string("connection closed during data read");
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
                if (n == 0)
                    co_return std::string("connection closed during header read");
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

        if (length > 16640)
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
            if (n == 0)
                co_return std::string("connection closed during data read");
            got += n;
        }

        co_return data;
    }

    Result<void> TLSConnection::send_encrypted_record(
        u8 inner_type, const std::vector<u8> &data, const u8 key[16], const u8 iv[12], u64 &seq) {
        auto ct = aead_encrypt(key, iv, seq, data.data(), static_cast<u32>(data.size()), inner_type);
        seq++;
        auto r = send_raw_record(APPLICATION_DATA, ct);
        if (r.is_err())
            return r;
        return {};
    }

    Result<std::vector<u8>> TLSConnection::read_encrypted_record(const u8 key[16], const u8 iv[12], u64 &seq) {
        u8 type = 0;
        auto r = read_raw_record(&type);
        if (r.is_err())
            return std::string("read encrypted: " + r.unwrap_err());
        auto &ct = r.unwrap();
        if (ct.empty())
            return std::vector<u8>();

        if (type == CHANGE_CIPHER_SPEC) {
            return read_encrypted_record(key, iv, seq);
        }

        u8 inner_type = 0;
        auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
        seq++;
        if (pt.empty() && inner_type == 0)
            return std::string("decryption failed");

        if (pt.empty())
            return std::vector<u8>();

        return pt;
    }

    async::task<bool> TLSConnection::send_encrypted_record_async(
        u8 inner_type, const std::vector<u8> &data, const u8 key[16], const u8 iv[12], u64 &seq) {
        auto ct = aead_encrypt(key, iv, seq, data.data(), static_cast<u32>(data.size()), inner_type);
        seq++;
        auto r = co_await send_raw_record_async(APPLICATION_DATA, ct);
        co_return r;
    }

    async::task<std::vector<u8>> TLSConnection::read_encrypted_record_async(const u8 key[16],
                                                                            const u8 iv[12],
                                                                            u64 &seq) {
        u8 type = 0;
        auto r = co_await read_raw_record_async(&type);
        if (r.is_err())
            co_return std::string("read encrypted: ") + r.unwrap_err();
        auto ct = r.unwrap();
        if (ct.empty())
            co_return std::vector<u8>();

        if (type == CHANGE_CIPHER_SPEC) {
            auto r2 = co_await read_encrypted_record_async(key, iv, seq);
            co_return r2;
        }

        u8 inner_type = 0;
        auto pt = aead_decrypt(key, iv, seq, ct.data(), static_cast<u32>(ct.size()), inner_type);
        seq++;
        if (pt.empty() && inner_type == 0)
            co_return std::string("decryption failed");

        co_return pt;
    }

}  // namespace browser::net::tls
