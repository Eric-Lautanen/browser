#include "http_client.hpp"

namespace browser::net {

TrackerBlocker* HTTPClient::tracker_ = nullptr;

HTTPClient::HTTPClient() = default;
HTTPClient::~HTTPClient() = default;
HTTPClient::HTTPClient(HTTPClient&&) noexcept = default;
HTTPClient& HTTPClient::operator=(HTTPClient&&) noexcept = default;

bool HTTPClient::is_connected() const {
    if (http1_ && http1_->is_connected()) return true;
    if (http2_ && http2_->is_connected()) return true;
    return false;
}

void HTTPClient::close() {
    if (http1_) { http1_->close(); http1_.reset(); }
    if (http2_) { http2_->close(); http2_.reset(); }
    if (tls_) { tls_->close(); tls_.reset(); }
    tcp_.close();
}

Result<void> HTTPClient::connect_if_needed(const http::Request& req) {
    if (tracker_ && tracker_->should_block(req.url.to_string())) {
        return std::string("blocked by tracker blocker");
    }

    use_tls_ = (req.url.scheme == "https");
    std::string host = req.url.host;
    if (!host.empty() && host.front() == '[' && host.back() == ']')
        host = host.substr(1, host.size() - 2);
    u16 port = req.url.port != 0 ? req.url.port : req.url.default_port();

    // If already connected to the right host, reuse
    if (tcp_.is_open() && tcp_.host() == host && tcp_.port() == port) {
        if (http1_ && http1_->is_connected()) return {};
        if (http2_ && http2_->is_connected()) return {};
    }

    close();

    // Open TCP connection
    ConnectionConfig cfg;
    cfg.connect_timeout_ms = 3000;
    cfg.read_timeout_ms = 5000;
    auto r = tcp_.open(host, port, cfg);
    if (r.is_err()) return std::string("connect: " + r.unwrap_err());

    // If HTTPS, perform TLS handshake then ALPN dispatch
    if (use_tls_) {
        tls_ = std::make_unique<tls::TLSConnection>();
        auto tr = tls_->connect(&tcp_, host);
        if (tr.is_err()) {
            close();
            return std::string("tls: " + tr.unwrap_err());
        }

        std::string alpn = tls_->negotiated_alpn();
        tls::TLSConnection* raw_tls = tls_.release();
        if (alpn == "h2") {
            http2_ = std::make_unique<http2::HTTP2Client>();
            auto h2r = http2_->connect(host, port, true, &tcp_, raw_tls);
            if (h2r.is_err()) {
                delete raw_tls;
                close();
                return std::string("h2 connect: " + h2r.unwrap_err());
            }
        } else {
            http1_ = std::make_unique<http::HTTP1Client>();
            auto h1r = http1_->connect(host, port, true, &tcp_, raw_tls);
            if (h1r.is_err()) {
                delete raw_tls;
                close();
                return std::string("h1 connect: " + h1r.unwrap_err());
            }
        }
    } else {
        http1_ = std::make_unique<http::HTTP1Client>();
        auto h1r = http1_->connect(host, port, false, &tcp_, nullptr);
        if (h1r.is_err()) {
            close();
            return std::string("h1 connect: " + h1r.unwrap_err());
        }
    }

    return {};
}

Result<http::Response> HTTPClient::fetch(const http::Request& req) {
    auto cr = connect_if_needed(req);
    if (cr.is_err()) return std::string("fetch: " + cr.unwrap_err());

    if (http2_) {
        return http2_->execute(req);
    } else if (http1_) {
        return http1_->execute(req);
    }

    return std::string("no client available");
}

Result<http::Response> HTTPClient::get(const std::string& url_str) {
    auto url_r = URL::parse(url_str);
    if (url_r.is_err()) return std::string("bad url: " + url_r.unwrap_err());

    http::Request req;
    req.method = http::Method::GET;
    req.url = url_r.unwrap();
    {
        std::string host_hdr = req.url.host;
        if (req.url.port != 0 && req.url.port != req.url.default_port())
            host_hdr += ":" + std::to_string(req.url.port);
        req.headers.set("Host", host_hdr);
    }
    req.headers.set("Connection", "close");

    return fetch(req);
}

} // namespace browser::net
