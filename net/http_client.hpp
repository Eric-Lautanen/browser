#pragma once
#include "../async/task.hpp"
#include "../tests/utility.hpp"
#include "connection.hpp"
#include "cookie_jar.hpp"
#include "hsts.hpp"
#include "http.hpp"
#include "http2.hpp"
#include "tls.hpp"
#include "tracker_blocker.hpp"
#include "url.hpp"

#include <memory>
#include <string>

namespace browser::net {

    class HTTPClient {
    public:
        HTTPClient();
        ~HTTPClient();
        HTTPClient(HTTPClient &&) noexcept;
        HTTPClient &operator=(HTTPClient &&) noexcept;
        HTTPClient(const HTTPClient &) = delete;

        static void set_tracker_blocker(TrackerBlocker *tb) { tracker_ = tb; }
        static CookieJar &cookie_jar() {
            static CookieJar jar;
            return jar;
        }
        static HSTSManager &hsts_manager() {
            static HSTSManager mgr;
            return mgr;
        }

        void set_page_url(const std::string &url_str) { page_url_ = url_str; }
        const std::string &page_url() const { return page_url_; }

        Result<http::Response> fetch(const http::Request &req);
        Result<http::Response> get(const std::string &url_str);
        void close();
        bool is_connected() const;
        async::task<http::Response> fetch_async(const http::Request &req);
        async::task<http::Response> get_async(const std::string &url_str);

    private:
        // N-P7: single attempt (connect + cookies/origin + execute) used by the
        // fetch wrappers, which add keep-alive stale-connection retry around it.
        Result<http::Response> execute_request(const http::Request &req);
        async::task<http::Response> execute_request_async(const http::Request &req);

        Connection tcp_;
        std::unique_ptr<tls::TLSConnection> tls_;
        std::unique_ptr<http::HTTP1Client> http1_;
        std::unique_ptr<http2::HTTP2Client> http2_;
        bool use_tls_ = false;
        static TrackerBlocker *tracker_;
        std::string page_url_;

        Result<void> connect_if_needed(const http::Request &req);
        async::task<bool> connect_if_needed_async(const http::Request &req);
    };

}  // namespace browser::net
