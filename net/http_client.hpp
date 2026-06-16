#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "url.hpp"
#include "connection.hpp"
#include "tls.hpp"
#include "http.hpp"
#include "http2.hpp"
#include "tracker_blocker.hpp"
#include <string>
#include <memory>

namespace browser::net {

class HTTPClient {
public:
    HTTPClient();
    ~HTTPClient();
    HTTPClient(HTTPClient&&) noexcept;
    HTTPClient& operator=(HTTPClient&&) noexcept;
    HTTPClient(const HTTPClient&) = delete;

    static void set_tracker_blocker(TrackerBlocker* tb) { tracker_ = tb; }

    Result<http::Response> fetch(const http::Request& req);
    Result<http::Response> get(const std::string& url_str);
    void close();
    bool is_connected() const;

    // Async methods
    async::task<http::Response> fetch_async(const http::Request& req);
    async::task<http::Response> get_async(const std::string& url_str);

private:
    Connection tcp_;
    std::unique_ptr<tls::TLSConnection> tls_;
    std::unique_ptr<http::HTTP1Client> http1_;
    std::unique_ptr<http2::HTTP2Client> http2_;
    bool use_tls_ = false;
    static TrackerBlocker* tracker_;

    Result<void> connect_if_needed(const http::Request& req);
    async::task<bool> connect_if_needed_async(const http::Request& req);
};

} // namespace browser::net

