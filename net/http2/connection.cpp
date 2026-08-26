#include "connection.hpp"

#include "internal.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace browser::net::http2 {

    namespace {

        inline constexpr u32 kFlagEndStream = 0x01;
        inline constexpr u32 kFlagEndHeaders = 0x04;

        enum class Action { Continue, Done, Fail };

        // Shared response state machine for both execute paths (dedupes the
        // previously diverged execute/execute_async). Handles header-block
        // fragmentation (HEADERS+CONTINUATION decoded once, N-C7), trailers,
        // DATA accumulation and RST_STREAM.
        struct ResponseAssembler {
            u32 stream_id = 0;
            HPack &hpack;

            std::vector<HPackEntry> entries;
            std::vector<u8> frag;     // header-block fragment accumulator
            bool collecting = false;  // HEADERS seen, awaiting END_HEADERS
            bool headers_done = false;
            bool in_trailers = false;  // N-C7: header block after body is trailers
            bool has_body = false;
            bool done = false;
            std::vector<u8> body;

            explicit ResponseAssembler(u32 sid, HPack &hp) : stream_id(sid), hpack(hp) {}

            // Consumes one frame belonging to our stream. On Done the response
            // is complete; consumed receives DATA bytes for window updates.
            Action on_frame(const FrameHeader &fh, const std::vector<u8> &payload, u32 &consumed, std::string &err) {
                consumed = 0;
                switch (fh.type) {
                    case HEADERS:
                    case CONTINUATION: {
                        if (!collecting) {
                            collecting = true;
                            frag.clear();
                            if (headers_done)
                                in_trailers = true;  // N-C7: trailer HEADERS after DATA
                        }
                        frag.insert(frag.end(), payload.begin(), payload.begin() + fh.length);

                        const bool end_headers = (fh.flags & kFlagEndHeaders) != 0;
                        const bool end_stream = (fh.flags & kFlagEndStream) != 0;
                        if (end_headers) {
                            auto decoded = hpack.decode(frag.data(), static_cast<u32>(frag.size()));
                            frag.clear();
                            collecting = false;
                            if (in_trailers) {
                                // Trailers are not merged into the response headers.
                                in_trailers = false;
                            } else {
                                entries.insert(entries.end(), decoded.begin(), decoded.end());
                                headers_done = true;
                            }
                            if (end_stream)
                                done = true;
                        }
                        return done ? Action::Done : Action::Continue;
                    }

                    case DATA: {
                        if (!collecting && !headers_done) {
                            err = "DATA before HEADERS complete";
                            return Action::Fail;
                        }
                        has_body = true;
                        body.insert(body.end(), payload.begin(), payload.begin() + fh.length);
                        consumed = fh.length;
                        if (fh.flags & kFlagEndStream)
                            done = true;
                        return done ? Action::Done : Action::Continue;
                    }

                    case RST_STREAM: {
                        u32 err_code = 0;
                        if (fh.length >= 4) {
                            err_code = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                                       (static_cast<u32>(payload[2]) << 8) | payload[3];
                        }
                        err = "RST_STREAM: err=" + std::to_string(err_code);
                        return Action::Fail;
                    }

                    default:
                        return Action::Continue;
                }
            }
        };

        // Plans DATA frame chunk sizes honouring the peer's frame-size limit and
        // both flow-control windows (N-C3). Returns the chunk length starting at
        // `off`; 0 means "must wait for a WINDOW_UPDATE".
        inline u32 data_chunk_len(
            std::size_t remaining, std::size_t off, u32 max_frame, u32 conn_window, u32 stream_window) {
            u64 allowed = std::min<std::size_t>({remaining - off, max_frame, conn_window, stream_window});
            return static_cast<u32>(allowed);
        }

    }  // namespace

    HTTP2Client::HTTP2Client() = default;
    HTTP2Client::~HTTP2Client() = default;
    HTTP2Client::HTTP2Client(HTTP2Client &&) noexcept = default;
    HTTP2Client &HTTP2Client::operator=(HTTP2Client &&) noexcept = default;

    bool HTTP2Client::is_connected() const {
        if (!tcp_.valid() || !tcp_->is_open())
            return false;
        if (use_tls_ && (!tls_ || !tls_->is_connected()))
            return false;
        return connected_;
    }

    void HTTP2Client::close() {
        connected_ = false;
        if (tls_) {
            tls_->close();
            tls_.reset();
        }
        // Safe on a borrowed socket: Connection::close() is idempotent and the
        // borrowed object itself is owned (and destroyed) by the caller.
        tcp_.close();
        next_stream_id_ = 1;
        server_window_ = 65535;
        client_window_ = 65535;
        server_max_frame_size_ = 16384;
        server_header_table_size_ = 4096;
        server_initial_window_size_ = 65535;
        server_enable_push_ = true;
        hpack_ = HPack();
    }

    std::vector<HPackEntry> HTTP2Client::request_to_hpack(const http::Request &req) {
        std::vector<HPackEntry> entries;

        std::string method_str;
        switch (req.method) {
            case http::Method::GET:
                method_str = "GET";
                break;
            case http::Method::POST:
                method_str = "POST";
                break;
            case http::Method::HEAD:
                method_str = "HEAD";
                break;
            case http::Method::PUT:
                method_str = "PUT";
                break;
            case http::Method::DELETE:
                method_str = "DELETE";
                break;
            case http::Method::CONNECT:
                method_str = "CONNECT";
                break;
            case http::Method::OPTIONS:
                method_str = "OPTIONS";
                break;
            case http::Method::PATCH:
                method_str = "PATCH";
                break;
        }
        entries.push_back({":method", method_str});
        entries.push_back({":scheme", req.url.scheme});

        std::string authority = req.url.host;
        if (!authority.empty() && authority.front() == '[' && authority.back() == ']')
            authority = authority.substr(1, authority.size() - 2);
        if (req.url.port != 0 && req.url.port != req.url.default_port())
            authority += ":" + std::to_string(req.url.port);
        entries.push_back({":authority", authority});

        std::string path = req.url.path.empty() ? "/" : req.url.path;
        if (!req.url.query.empty())
            path += "?" + req.url.query;
        entries.push_back({":path", path});

        for (auto &[k, v] : req.headers.all()) {
            entries.push_back({k, v});
        }
        return entries;
    }

    Result<http::Response> HTTP2Client::hpack_to_response(const std::vector<HPackEntry> &entries) {
        http::Response resp;
        http::Headers headers;
        for (auto &e : entries) {
            if (e.name == ":status") {
                char *end = nullptr;
                long code = std::strtol(e.value.c_str(), &end, 10);
                if (*end != '\0' || code < 100 || code > 599)
                    return std::string("bad :status in HTTP/2 response");
                resp.status.code = static_cast<u16>(code);
                resp.status.reason = http_status_reason(static_cast<u16>(code));
            } else if (!e.name.empty() && e.name[0] == ':') {
            } else {
                headers.set(e.name, e.value);
            }
        }
        resp.headers = std::move(headers);
        resp.http_version = "HTTP/2";
        if (resp.status.code == 0)
            return std::string("missing :status pseudo-header");
        return resp;
    }

    Result<void> HTTP2Client::send_preface() {
        static const u8 kPreface[24] = {0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32,
                                        0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a};
        if (use_tls_) {
            auto r = tls_->send_all(kPreface, 24);
            if (r.is_err())
                return std::string("send preface: " + r.unwrap_err());
        } else {
            auto r = tcp_->send_all(kPreface, 24);
            if (r.is_err())
                return std::string("send preface: " + r.unwrap_err());
        }
        return send_settings({});
    }

    Result<void> HTTP2Client::read_and_process_settings() {
        while (true) {
            auto fh_r = read_frame();
            if (fh_r.is_err())
                return std::string("read settings: " + fh_r.unwrap_err());
            auto fh = fh_r.unwrap();

            if (fh.length > server_max_frame_size_)
                return std::string("frame exceeds max frame size");

            std::vector<u8> payload;
            auto pr = read_frame_payload(fh, payload);
            if (pr.is_err())
                return std::string("read settings payload: " + pr.unwrap_err());

            if (fh.type == SETTINGS && fh.stream_id == 0) {
                if (fh.flags & 0x01) {
                    continue;
                }
                u32 sp = 0;
                while (sp + 6 <= fh.length) {
                    u16 id = static_cast<u16>((payload[sp] << 8) | payload[sp + 1]);
                    u32 val = (static_cast<u32>(payload[sp + 2]) << 24) | (static_cast<u32>(payload[sp + 3]) << 16) |
                              (static_cast<u32>(payload[sp + 4]) << 8) | payload[sp + 5];
                    sp += 6;
                    switch (id) {
                        case SETTINGS_HEADER_TABLE_SIZE:
                            server_header_table_size_ = val;
                            hpack_.set_max_table_size(val);
                            break;
                        case SETTINGS_ENABLE_PUSH:
                            server_enable_push_ = (val != 0);
                            break;
                        case SETTINGS_MAX_CONCURRENT_STREAMS:
                            break;
                        case SETTINGS_INITIAL_WINDOW_SIZE:
                            server_window_ = server_window_ + (val - server_initial_window_size_);
                            server_initial_window_size_ = val;
                            break;
                        case SETTINGS_MAX_FRAME_SIZE:
                            if (val < 16384 || val > 16777215)
                                return std::string("invalid max frame size");
                            server_max_frame_size_ = val;
                            break;
                        case SETTINGS_MAX_HEADER_LIST_SIZE:
                            break;
                    }
                }
                return send_frame(SETTINGS, 0x01, 0, {});
            } else if (fh.type == WINDOW_UPDATE && fh.stream_id == 0 && fh.length >= 4) {
                u32 increment = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                                (static_cast<u32>(payload[2]) << 8) | payload[3];
                server_window_ += increment;
            } else if (fh.type == GOAWAY && fh.stream_id == 0) {
                return std::string("server sent GOAWAY during settings");
            } else if (fh.type == PING && (fh.flags & 0x01) == 0) {
                auto sr = send_frame(PING, 0x01, 0, payload);
                if (sr.is_err())
                    return sr;
            }
        }
        return {};
    }

    Result<void> HTTP2Client::connect(const std::string &host,
                                      u16 port,
                                      bool use_tls,
                                      Connection *existing_tcp,
                                      std::unique_ptr<tls::TLSConnection> existing_tls) {
        close();
        use_tls_ = use_tls;

        if (existing_tcp && existing_tls) {
            // Adopt caller's established connection; take ownership of the TLS layer.
            tcp_.adopt(*existing_tcp);
            tls_ = std::move(existing_tls);
        } else {
            ConnectionConfig cfg;
            cfg.connect_timeout_ms = 10000;
            cfg.read_timeout_ms = 30000;
            auto r = tcp_.obtain().open(host, port, cfg);
            if (r.is_err())
                return std::string("connect: " + r.unwrap_err());

            if (use_tls_) {
                tls_ = std::make_unique<tls::TLSConnection>();
                auto tr = tls_->connect(&tcp_.get(), host);
                if (tr.is_err()) {
                    close();
                    return std::string("tls: " + tr.unwrap_err());
                }
                if (tls_->negotiated_alpn() != "h2") {
                    close();
                    return std::string("server does not support h2, got: " + tls_->negotiated_alpn());
                }
            }
        }

        auto pr = send_preface();
        if (pr.is_err()) {
            close();
            return pr;
        }

        auto sr = read_and_process_settings();
        if (sr.is_err()) {
            close();
            return sr;
        }

        connected_ = true;
        return {};
    }

    Result<http::Response> HTTP2Client::execute(const http::Request &req) {
        if (!connected_)
            return std::string("not connected");

        u32 stream_id = next_stream_id_;
        next_stream_id_ += 2;

        auto entries = request_to_hpack(req);
        auto hpack_data = hpack_.encode(entries);

        {
            u8 flags = kFlagEndHeaders;
            if (req.body.empty())
                flags |= kFlagEndStream;
            auto sr = send_frame(HEADERS, flags, stream_id, hpack_data);
            if (sr.is_err())
                return std::string("send headers: " + sr.unwrap_err());
        }

        // N-C3: send the request body as DATA frames honouring frame size and
        // flow-control windows; wait for WINDOW_UPDATE when the window is dry.
        u32 stream_window = server_initial_window_size_;
        std::size_t off = 0;
        while (off < req.body.size()) {
            u32 len = data_chunk_len(req.body.size(), off, server_max_frame_size_, server_window_, stream_window);
            if (len == 0) {
                // Window dry: pump one frame (window updates / pings) and retry.
                auto fh_r = read_frame();
                if (fh_r.is_err()) {
                    close();
                    return std::string("read frame: " + fh_r.unwrap_err());
                }
                auto fh = fh_r.unwrap();
                std::vector<u8> payload;
                if (fh.length > 0) {
                    auto pr = read_frame_payload(fh, payload);
                    if (pr.is_err()) {
                        close();
                        return std::string("read payload: " + pr.unwrap_err());
                    }
                }
                if (fh.type == GOAWAY)
                    return std::string("GOAWAY while sending body");
                if (fh.type == PING && (fh.flags & 0x01) == 0) {
                    auto sr = send_frame(PING, 0x01, 0, payload);
                    if (sr.is_err())
                        return std::string("ping ack: " + sr.unwrap_err());
                }
                if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                    u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                              (static_cast<u32>(payload[2]) << 8) | payload[3];
                    if (fh.stream_id == 0)
                        server_window_ += inc;
                    else if (fh.stream_id == stream_id)
                        stream_window += inc;
                }
                continue;
            }
            const bool last = (off + len >= req.body.size());
            const u8 flags = last ? kFlagEndStream : 0;
            std::vector<u8> chunk(req.body.begin() + off, req.body.begin() + off + len);
            auto sr = send_frame(DATA, flags, stream_id, chunk);
            if (sr.is_err())
                return std::string("send body: " + sr.unwrap_err());
            server_window_ -= len;
            stream_window -= len;
            off += len;
        }

        ResponseAssembler asm_(stream_id, hpack_);
        std::string err;
        while (!asm_.done) {
            auto fh_r = read_frame();
            if (fh_r.is_err()) {
                close();
                return std::string("read frame: " + fh_r.unwrap_err());
            }
            auto fh = fh_r.unwrap();

            if (fh.length > server_max_frame_size_) {
                close();
                return std::string("frame exceeds max frame size");
            }

            std::vector<u8> payload;
            if (fh.length > 0) {
                auto pr = read_frame_payload(fh, payload);
                if (pr.is_err()) {
                    close();
                    return std::string("read payload: " + pr.unwrap_err());
                }
            }

            // Connection-level frames.
            if (fh.stream_id == 0) {
                if (fh.type == GOAWAY)
                    return std::string("GOAWAY received");
                if (fh.type == PING && (fh.flags & 0x01) == 0) {
                    auto sr = send_frame(PING, 0x01, 0, payload);
                    if (sr.is_err()) {
                        close();
                        return std::string("ping ack: " + sr.unwrap_err());
                    }
                }
                if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                    u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                              (static_cast<u32>(payload[2]) << 8) | payload[3];
                    server_window_ += inc;
                }
                continue;
            }

            if (fh.stream_id != asm_.stream_id)
                continue;

            u32 consumed = 0;
            switch (asm_.on_frame(fh, payload, consumed, err)) {
                case Action::Continue:
                    break;
                case Action::Done:
                    break;
                case Action::Fail:
                    close();
                    return err;
            }
            if (consumed > 0) {
                // Restore the windows we just consumed.
                auto sr1 = send_window_update(asm_.stream_id, consumed);
                auto sr2 = send_window_update(0, consumed);
                if (sr1.is_err() || sr2.is_err()) {
                    close();
                    return std::string("window update failed");
                }
            }
        }

        auto resp_r = hpack_to_response(asm_.entries);
        if (resp_r.is_err())
            return resp_r;
        auto resp = resp_r.unwrap();

        if (asm_.has_body) {
            if (resp.headers.has("content-length")) {
                std::string cl = resp.headers.get("content-length");
                char *cl_end = nullptr;
                long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
                if (*cl_end == '\0' && cl_val >= 0) {
                    if (asm_.body.size() > static_cast<std::size_t>(cl_val))
                        asm_.body.resize(static_cast<std::size_t>(cl_val));
                }
            }
            resp.body = std::move(asm_.body);
        }

        return resp;
    }

    async::task<http::Response> HTTP2Client::execute_async(const http::Request &req) {
        if (!connected_)
            co_return std::string("not connected");

        u32 stream_id = next_stream_id_;
        next_stream_id_ += 2;

        auto entries = request_to_hpack(req);
        auto hpack_data = hpack_.encode(entries);

        {
            u8 flags = kFlagEndHeaders;
            if (req.body.empty())
                flags |= kFlagEndStream;
            FrameHeader hdr;
            hdr.length = static_cast<u32>(hpack_data.size());
            hdr.type = HEADERS;
            hdr.flags = flags;
            hdr.stream_id = stream_id;
            auto frame = serialize_frame(hdr, hpack_data.data());
            auto r = use_tls_ ? co_await tls_->send_all_async(frame.data(), static_cast<u32>(frame.size()))
                              : co_await tcp_->send_all_async(frame.data(), static_cast<u32>(frame.size()));
            if (r.is_err())
                co_return std::string("send headers: ") + r.unwrap_err();
        }

        // N-C3: request body as DATA frames; pump frames while the window is dry.
        u32 stream_window = server_initial_window_size_;
        std::size_t off = 0;
        while (off < req.body.size()) {
            u32 len = data_chunk_len(req.body.size(), off, server_max_frame_size_, server_window_, stream_window);
            if (len == 0) {
                u8 header[9];
                u32 got = 0;
                while (got < 9) {
                    auto rr = use_tls_ ? co_await tls_->receive_async(header + got, 9 - got)
                                       : co_await tcp_->receive_async(header + got, 9 - got);
                    if (rr.is_err())
                        co_return std::string("read frame header: ") + rr.unwrap_err();
                    u32 n = rr.unwrap();
                    if (n == 0)
                        co_return std::string("connection closed");
                    got += n;
                }
                u32 pos = 0;
                auto fh_r = parse_frame_header(header, 9, pos);
                if (fh_r.is_err())
                    co_return fh_r.unwrap_err();
                auto fh = fh_r.unwrap();
                std::vector<u8> payload;
                if (fh.length > 0) {
                    payload.resize(fh.length);
                    u32 pgot = 0;
                    while (pgot < fh.length) {
                        auto rr = use_tls_ ? co_await tls_->receive_async(payload.data() + pgot, fh.length - pgot)
                                           : co_await tcp_->receive_async(payload.data() + pgot, fh.length - pgot);
                        if (rr.is_err())
                            co_return std::string("read payload: ") + rr.unwrap_err();
                        u32 n = rr.unwrap();
                        if (n == 0)
                            co_return std::string("connection closed");
                        pgot += n;
                    }
                }
                if (fh.type == GOAWAY)
                    co_return std::string("GOAWAY while sending body");
                if (fh.type == PING && (fh.flags & 0x01) == 0) {
                    FrameHeader ack_hdr;
                    ack_hdr.length = fh.length;
                    ack_hdr.type = PING;
                    ack_hdr.flags = kFlagEndStream;
                    ack_hdr.stream_id = 0;
                    auto ack = serialize_frame(ack_hdr, payload.data());
                    auto ar = use_tls_ ? co_await tls_->send_all_async(ack.data(), static_cast<u32>(ack.size()))
                                       : co_await tcp_->send_all_async(ack.data(), static_cast<u32>(ack.size()));
                    if (ar.is_err())
                        co_return std::string("ping ack: ") + ar.unwrap_err();
                }
                if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                    u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                              (static_cast<u32>(payload[2]) << 8) | payload[3];
                    if (fh.stream_id == 0)
                        server_window_ += inc;
                    else if (fh.stream_id == stream_id)
                        stream_window += inc;
                }
                continue;
            }
            const bool last = (off + len >= req.body.size());
            const u8 flags = last ? kFlagEndStream : 0;
            std::vector<u8> chunk(req.body.begin() + off, req.body.begin() + off + len);
            auto sr = send_frame(DATA, flags, stream_id, chunk);
            if (sr.is_err())
                co_return std::string("send body: ") + sr.unwrap_err();
            server_window_ -= len;
            stream_window -= len;
            off += len;
        }

        ResponseAssembler asm_(stream_id, hpack_);
        std::string err;
        while (!asm_.done) {
            u8 header[9];
            u32 got = 0;
            while (got < 9) {
                auto r = use_tls_ ? co_await tls_->receive_async(header + got, 9 - got)
                                  : co_await tcp_->receive_async(header + got, 9 - got);
                if (r.is_err())
                    co_return std::string("read frame header: ") + r.unwrap_err();
                u32 n = r.unwrap();
                if (n == 0)
                    co_return std::string("connection closed");
                got += n;
            }

            u32 pos = 0;
            auto fh_r = parse_frame_header(header, 9, pos);
            if (fh_r.is_err())
                co_return fh_r.unwrap_err();
            auto fh = fh_r.unwrap();

            if (fh.length > server_max_frame_size_) {
                close();
                co_return std::string("frame exceeds max frame size");
            }

            std::vector<u8> payload;
            if (fh.length > 0) {
                payload.resize(fh.length);
                u32 pgot = 0;
                while (pgot < fh.length) {
                    auto r = use_tls_ ? co_await tls_->receive_async(payload.data() + pgot, fh.length - pgot)
                                      : co_await tcp_->receive_async(payload.data() + pgot, fh.length - pgot);
                    if (r.is_err())
                        co_return std::string("read payload: ") + r.unwrap_err();
                    u32 n = r.unwrap();
                    if (n == 0)
                        co_return std::string("connection closed");
                    pgot += n;
                }
            }

            if (fh.stream_id == 0) {
                if (fh.type == GOAWAY)
                    co_return std::string("GOAWAY received");
                if (fh.type == PING && (fh.flags & 0x01) == 0) {
                    FrameHeader ack_hdr;
                    ack_hdr.length = fh.length;
                    ack_hdr.type = PING;
                    ack_hdr.flags = kFlagEndStream;
                    ack_hdr.stream_id = 0;
                    auto ack = serialize_frame(ack_hdr, payload.data());
                    auto ar = use_tls_ ? co_await tls_->send_all_async(ack.data(), static_cast<u32>(ack.size()))
                                       : co_await tcp_->send_all_async(ack.data(), static_cast<u32>(ack.size()));
                    if (ar.is_err()) {
                        close();
                        co_return std::string("ping ack: ") + ar.unwrap_err();
                    }
                }
                if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                    u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                              (static_cast<u32>(payload[2]) << 8) | payload[3];
                    server_window_ += inc;
                }
                continue;
            }

            if (fh.stream_id != asm_.stream_id)
                continue;

            u32 consumed = 0;
            switch (asm_.on_frame(fh, payload, consumed, err)) {
                case Action::Continue:
                    break;
                case Action::Done:
                    break;
                case Action::Fail:
                    close();
                    co_return err;
            }
            if (consumed > 0) {
                auto sr1 = send_window_update(asm_.stream_id, consumed);
                auto sr2 = send_window_update(0, consumed);
                if (sr1.is_err() || sr2.is_err()) {
                    close();
                    co_return std::string("window update failed");
                }
            }
        }

        auto resp_r = hpack_to_response(asm_.entries);
        if (resp_r.is_err())
            co_return resp_r.unwrap_err();
        auto resp = resp_r.unwrap();

        if (asm_.has_body) {
            if (resp.headers.has("content-length")) {
                std::string cl = resp.headers.get("content-length");
                char *cl_end = nullptr;
                long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
                if (*cl_end == '\0' && cl_val >= 0) {
                    if (asm_.body.size() > static_cast<std::size_t>(cl_val))
                        asm_.body.resize(static_cast<std::size_t>(cl_val));
                }
            }
            resp.body = std::move(asm_.body);
        }

        co_return resp;
    }

}  // namespace browser::net::http2
