#include "connection.hpp"
#include "internal.hpp"

#include <cstring>

namespace browser::net::http2 {

    std::vector<u8> HTTP2Client::serialize_frame(const FrameHeader &hdr, const u8 *payload) {
        std::vector<u8> frame;
        write_u24_be(frame, hdr.length);
        frame.push_back(static_cast<u8>(hdr.type));
        frame.push_back(hdr.flags);
        write_u32_be(frame, hdr.stream_id & 0x7FFFFFFFu);
        if (payload && hdr.length > 0)
            frame.insert(frame.end(), payload, payload + hdr.length);
        return frame;
    }

    Result<FrameHeader> HTTP2Client::parse_frame_header(const u8 *data, u32 len, u32 &pos) {
        if (pos + 9 > len)
            return std::string("frame header truncated");
        FrameHeader hdr;
        hdr.length = peek_u24_be(data, len, pos);
        pos += 3;
        if (pos >= len)
            return std::string("frame header truncated at type");
        hdr.type = static_cast<FrameType>(data[pos++]);
        if (pos >= len)
            return std::string("frame header truncated at flags");
        hdr.flags = data[pos++];
        hdr.stream_id = read_u32_be(data, len, pos) & 0x7FFFFFFFu;
        return hdr;
    }

    Result<void> HTTP2Client::send_frame(FrameType type, u8 flags, u32 stream_id, const std::vector<u8> &payload) {
        FrameHeader hdr;
        hdr.length = static_cast<u32>(payload.size());
        hdr.type = type;
        hdr.flags = flags;
        hdr.stream_id = stream_id;
        auto frame = serialize_frame(hdr, payload.data());
        if (use_tls_) {
            return tls_->send_all(frame.data(), static_cast<u32>(frame.size()));
        } else {
            return tcp_.send_all(frame.data(), static_cast<u32>(frame.size()));
        }
    }

    Result<u32> HTTP2Client::read_some(u8 *buf, u32 len) {
        if (use_tls_)
            return tls_->receive(buf, len);
        return tcp_.receive(buf, len);
    }

    Result<FrameHeader> HTTP2Client::read_frame() {
        u8 header[9];
        u32 got = 0;
        while (got < 9) {
            auto r = read_some(header + got, 9 - got);
            if (r.is_err())
                return std::string("read frame header: " + r.unwrap_err());
            u32 n = r.unwrap();
            if (n == 0)
                return std::string("connection closed during frame header");
            got += n;
        }
        u32 pos = 0;
        return parse_frame_header(header, 9, pos);
    }

    Result<u32> HTTP2Client::read_frame_payload(const FrameHeader &hdr, std::vector<u8> &out) {
        if (hdr.length == 0) {
            out.clear();
            return 0u;
        }
        out.resize(hdr.length);
        u32 got = 0;
        while (got < hdr.length) {
            auto r = read_some(out.data() + got, hdr.length - got);
            if (r.is_err())
                return std::string("read frame payload: " + r.unwrap_err());
            u32 n = r.unwrap();
            if (n == 0)
                return std::string("connection closed during frame payload");
            got += n;
        }
        return got;
    }

    Result<void> HTTP2Client::send_settings(const std::vector<std::pair<u16, u32>> &settings) {
        std::vector<u8> payload;
        for (auto &s : settings) {
            write_u16_be(payload, s.first);
            write_u32_be(payload, s.second);
        }
        return send_frame(SETTINGS, 0, 0, payload);
    }

    Result<void> HTTP2Client::send_window_update(u32 stream_id, u32 increment) {
        std::vector<u8> payload(4);
        payload[0] = static_cast<u8>((increment >> 24) & 0x7F);
        payload[1] = static_cast<u8>((increment >> 16) & 0xFF);
        payload[2] = static_cast<u8>((increment >> 8) & 0xFF);
        payload[3] = static_cast<u8>(increment & 0xFF);
        return send_frame(WINDOW_UPDATE, 0, stream_id, payload);
    }

    Result<void> HTTP2Client::send_goaway(u32 last_stream_id, u32 error_code) {
        std::vector<u8> payload(8);
        payload[0] = static_cast<u8>((last_stream_id >> 24) & 0x7F);
        payload[1] = static_cast<u8>((last_stream_id >> 16) & 0xFF);
        payload[2] = static_cast<u8>((last_stream_id >> 8) & 0xFF);
        payload[3] = static_cast<u8>(last_stream_id & 0xFF);
        payload[4] = static_cast<u8>((error_code >> 24) & 0xFF);
        payload[5] = static_cast<u8>((error_code >> 16) & 0xFF);
        payload[6] = static_cast<u8>((error_code >> 8) & 0xFF);
        payload[7] = static_cast<u8>(error_code & 0xFF);
        return send_frame(GOAWAY, 0, 0, payload);
    }

}  // namespace browser::net::http2
