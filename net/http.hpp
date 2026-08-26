#pragma once
#include "../async/task.hpp"
#include "../tests/utility.hpp"
#include "connection.hpp"
#include "tls.hpp"
#include "url.hpp"

#include <cctype>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::net::http {

    // Hard cap on accumulated response bytes; guards against servers that
    // stream forever without completing the response (audit N-C1).
    inline constexpr u32 kMaxResponseBytes = 32u * 1024u * 1024u;

    // Whether a parsed response's body is fully contained by the input bytes.
    enum class BodyState {
        COMPLETE,        // framing satisfied (content-length met / last chunk seen)
        INCOMPLETE,      // explicit framing present but bytes still missing
        CLOSE_DELIMITED  // no framing; only connection close completes the body
    };

#ifdef DELETE
#undef DELETE
#endif
#ifdef CONNECT
#undef CONNECT
#endif

    enum class Method { GET, POST, HEAD, PUT, DELETE, CONNECT, OPTIONS, PATCH };

    struct Status {
        u16 code = 0;
        std::string reason;
    };

    class Headers {
    public:
        void set(const std::string &key, const std::string &value);
        std::string get(const std::string &key) const;
        bool has(const std::string &key) const;
        void remove(const std::string &key);
        std::string to_string() const;

        static Result<Headers> parse(const u8 *data, u32 len, u32 &consumed);

        const std::unordered_map<std::string, std::string> &all() const { return headers_; }

    private:
        static std::string normalize_key(const std::string &key) {
            std::string lk;
            lk.resize(key.size());
            for (std::size_t i = 0; i < key.size(); i++)
                lk[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(key[i])));
            return lk;
        }

        std::unordered_map<std::string, std::string> headers_;
    };

    struct Request {
        Method method = Method::GET;
        URL url;
        Headers headers;
        std::vector<u8> body;

        std::vector<u8> serialize() const;
    };

    struct Response {
        Status status;
        Headers headers;
        std::vector<u8> body;
        std::string http_version;

        // Parses a response from `len` bytes. When `state` is non-null it
        // receives whether the body is fully contained (Content-Length
        // satisfied, terminating chunk seen, no body expected), still missing
        // bytes under explicit framing, or delimited only by connection close.
        // Callers loop for more data until COMPLETE or connection close;
        // a close while INCOMPLETE is a truncation error (audit N-C1).
        static Result<Response> parse(const u8 *data, u32 len, BodyState *state = nullptr);
    };

    class HTTP1Client {
    public:
        HTTP1Client();
        ~HTTP1Client();
        HTTP1Client(HTTP1Client &&) noexcept;
        HTTP1Client &operator=(HTTP1Client &&) noexcept;
        HTTP1Client(const HTTP1Client &) = delete;

        Result<void> connect(const std::string &host,
                             u16 port,
                             bool use_tls,
                             Connection *existing_tcp = nullptr,
                             std::unique_ptr<tls::TLSConnection> existing_tls = nullptr);
        Result<Response> execute(const Request &req);
        void close();
        bool is_connected() const;

        async::task<Response> execute_async(const Request &req);

    private:
        // Borrows the caller's socket when adopting an existing connection;
        // owns one otherwise. Never destroys a borrowed socket.
        ConnectionRef tcp_;
        std::unique_ptr<tls::TLSConnection> tls_;
        bool use_tls_ = false;

        Result<void> connect_if_needed(const Request &req);
        Result<Response> read_response();
    };

}  // namespace browser::net::http
