#include "connection.hpp"

#include "internal.hpp"

#include <cstring>
#include <sstream>

namespace browser::net::http2 {

    HTTP2Client::HTTP2Client() = default;
    HTTP2Client::~HTTP2Client() = default;
    HTTP2Client::HTTP2Client(HTTP2Client &&) noexcept = default;
    HTTP2Client &HTTP2Client::operator=(HTTP2Client &&) noexcept = default;

    bool HTTP2Client::is_connected() const {
        if (!tcp_.is_open())
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
            auto r = tcp_.send_all(kPreface, 24);
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

    Result<void> HTTP2Client::connect(
        const std::string &host, u16 port, bool use_tls, Connection *existing_tcp, tls::TLSConnection *existing_tls) {
        close();
        use_tls_ = use_tls;

        if (existing_tcp && existing_tls) {
            tcp_ = std::move(*existing_tcp);
            tls_.reset(existing_tls);
        } else {
            ConnectionConfig cfg;
            cfg.connect_timeout_ms = 10000;
            cfg.read_timeout_ms = 30000;
            auto r = tcp_.open(host, port, cfg);
            if (r.is_err())
                return std::string("connect: " + r.unwrap_err());

            if (use_tls_) {
                tls_ = std::make_unique<tls::TLSConnection>();
                auto tr = tls_->connect(&tcp_, host);
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

        u8 flags = 0x04;
        if (req.body.empty())
            flags |= 0x01;
        auto sr = send_frame(HEADERS, flags, stream_id, hpack_data);
        if (sr.is_err())
            return std::string("send headers: " + sr.unwrap_err());

        std::vector<HPackEntry> resp_entries;
        bool headers_complete = false;
        bool has_body = false;
        std::vector<u8> body;

        while (true) {
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
            auto pr = read_frame_payload(fh, payload);
            if (pr.is_err()) {
                close();
                return std::string("read payload: " + pr.unwrap_err());
            }

            if (fh.stream_id == 0) {
                if (fh.type == GOAWAY) {
                    u32 last_stream = 0;
                    u32 err_code = 0;
                    if (fh.length >= 8) {
                        last_stream = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                                      (static_cast<u32>(payload[2]) << 8) | payload[3];
                        err_code = (static_cast<u32>(payload[4]) << 24) | (static_cast<u32>(payload[5]) << 16) |
                                   (static_cast<u32>(payload[6]) << 8) | payload[7];
                    } else if (fh.length >= 4) {
                        last_stream = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                                      (static_cast<u32>(payload[2]) << 8) | payload[3];
                    }
                    return std::string("GOAWAY: stream=" + std::to_string(last_stream) +
                                       " err=" + std::to_string(err_code));
                }
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

            if (fh.stream_id != stream_id) {
                continue;
            }

            if (fh.type == HEADERS || fh.type == CONTINUATION) {
                auto new_entries = hpack_.decode(payload.data(), fh.length);
                resp_entries.insert(resp_entries.end(), new_entries.begin(), new_entries.end());
                if (fh.flags & 0x04) {
                    headers_complete = true;
                }
                if (fh.flags & 0x01) {
                    has_body = false;
                    break;
                }
            } else if (fh.type == DATA) {
                if (!headers_complete) {
                    close();
                    return std::string("DATA before HEADERS complete");
                }
                if (fh.length > server_window_) {
                    close();
                    return std::string("flow control window exceeded");
                }
                has_body = true;
                body.insert(body.end(), payload.begin(), payload.begin() + fh.length);
                server_window_ -= fh.length;
                auto sr1 = send_window_update(stream_id, fh.length);
                auto sr2 = send_window_update(0, fh.length);
                if (sr1.is_err() || sr2.is_err()) {
                    close();
                    return std::string("window update failed");
                }
                if (fh.flags & 0x01) {
                    break;
                }
            } else if (fh.type == RST_STREAM) {
                u32 err_code = 0;
                if (fh.length >= 4) {
                    err_code = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                               (static_cast<u32>(payload[2]) << 8) | payload[3];
                }
                return std::string("RST_STREAM: err=" + std::to_string(err_code));
            } else if (fh.type == GOAWAY) {
                return std::string("server sent GOAWAY");
            } else if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                          (static_cast<u32>(payload[2]) << 8) | payload[3];
                server_window_ += inc;
            }
        }

        if (!headers_complete) {
            close();
            return std::string("incomplete headers");
        }

        auto resp_r = hpack_to_response(resp_entries);
        if (resp_r.is_err())
            return resp_r;
        auto resp = resp_r.unwrap();

        if (has_body) {
            if (resp.headers.has("content-length")) {
                std::string cl = resp.headers.get("content-length");
                char *cl_end = nullptr;
                long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
                if (*cl_end == '\0' && cl_val >= 0) {
                    if (body.size() > static_cast<std::size_t>(cl_val))
                        body.resize(static_cast<std::size_t>(cl_val));
                }
            }
            resp.body = std::move(body);
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
            u8 flags = 0x04;
            if (req.body.empty())
                flags |= 0x01;
            FrameHeader hdr;
            hdr.length = static_cast<u32>(hpack_data.size());
            hdr.type = HEADERS;
            hdr.flags = flags;
            hdr.stream_id = stream_id;
            auto frame = serialize_frame(hdr, hpack_data.data());
            if (use_tls_) {
                auto r = co_await tls_->send_all_async(frame.data(), static_cast<u32>(frame.size()));
                if (r.is_err())
                    co_return std::string("send headers: ") + r.unwrap_err();
            } else {
                auto r = co_await tcp_.send_all_async(frame.data(), static_cast<u32>(frame.size()));
                if (r.is_err())
                    co_return std::string("send headers: ") + r.unwrap_err();
            }
        }

        std::vector<HPackEntry> resp_entries;
        bool headers_complete = false;
        bool has_body = false;
        std::vector<u8> body;

        while (true) {
            u8 header[9];
            u32 got = 0;
            while (got < 9) {
                auto r = use_tls_ ? co_await tls_->receive_async(header + got, 9 - got)
                                  : co_await tcp_.receive_async(header + got, 9 - got);
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
                                      : co_await tcp_.receive_async(payload.data() + pgot, fh.length - pgot);
                    if (r.is_err())
                        co_return std::string("read payload: ") + r.unwrap_err();
                    u32 n = r.unwrap();
                    if (n == 0)
                        co_return std::string("connection closed");
                    pgot += n;
                }
            }

            if (fh.stream_id == 0) {
                if (fh.type == GOAWAY) {
                    co_return std::string("GOAWAY");
                }
                if (fh.type == PING && (fh.flags & 0x01) == 0) {
                    FrameHeader ack_hdr;
                    ack_hdr.length = fh.length;
                    ack_hdr.type = PING;
                    ack_hdr.flags = 0x01;
                    ack_hdr.stream_id = 0;
                    auto ack = serialize_frame(ack_hdr, payload.data());
                    if (use_tls_) {
                        auto r = co_await tls_->send_all_async(ack.data(), static_cast<u32>(ack.size()));
                        if (r.is_err()) {
                            close();
                            co_return std::string("ping ack: ") + r.unwrap_err();
                        }
                    } else {
                        auto r = co_await tcp_.send_all_async(ack.data(), static_cast<u32>(ack.size()));
                        if (r.is_err()) {
                            close();
                            co_return std::string("ping ack: ") + r.unwrap_err();
                        }
                    }
                }
                if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                    u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                              (static_cast<u32>(payload[2]) << 8) | payload[3];
                    server_window_ += inc;
                }
                continue;
            }

            if (fh.stream_id != stream_id)
                continue;

            if (fh.type == HEADERS || fh.type == CONTINUATION) {
                auto new_entries = hpack_.decode(payload.data(), fh.length);
                resp_entries.insert(resp_entries.end(), new_entries.begin(), new_entries.end());
                if (fh.flags & 0x04)
                    headers_complete = true;
                if (fh.flags & 0x01) {
                    has_body = false;
                    break;
                }
            } else if (fh.type == DATA) {
                if (!headers_complete) {
                    close();
                    co_return std::string("DATA before HEADERS");
                }
                if (fh.length > server_window_) {
                    close();
                    co_return std::string("flow control");
                }
                has_body = true;
                body.insert(body.end(), payload.begin(), payload.begin() + fh.length);
                server_window_ -= fh.length;
                {
                    auto sup1 = send_window_update(stream_id, fh.length);
                    auto sup2 = send_window_update(0, fh.length);
                    if (sup1.is_err() || sup2.is_err()) {
                        close();
                        co_return std::string("window update failed");
                    }
                }
                if (fh.flags & 0x01)
                    break;
            } else if (fh.type == RST_STREAM) {
                co_return std::string("RST_STREAM");
            } else if (fh.type == GOAWAY) {
                co_return std::string("GOAWAY");
            } else if (fh.type == WINDOW_UPDATE && fh.length >= 4) {
                u32 inc = (static_cast<u32>(payload[0]) << 24) | (static_cast<u32>(payload[1]) << 16) |
                          (static_cast<u32>(payload[2]) << 8) | payload[3];
                server_window_ += inc;
            }
        }

        if (!headers_complete) {
            close();
            co_return std::string("incomplete headers");
        }

        auto resp_r = hpack_to_response(resp_entries);
        if (resp_r.is_err())
            co_return resp_r.unwrap_err();
        auto resp = resp_r.unwrap();

        if (has_body) {
            if (resp.headers.has("content-length")) {
                std::string cl = resp.headers.get("content-length");
                char *cl_end = nullptr;
                long cl_val = std::strtol(cl.c_str(), &cl_end, 10);
                if (*cl_end == '\0' && cl_val >= 0) {
                    if (body.size() > static_cast<std::size_t>(cl_val))
                        body.resize(static_cast<std::size_t>(cl_val));
                }
            }
            resp.body = std::move(body);
        }

        co_return resp;
    }

}  // namespace browser::net::http2
