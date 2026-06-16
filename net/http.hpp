#pragma once
#include "../tests/utility.hpp"
#include "../async/task.hpp"
#include "url.hpp"
#include "connection.hpp"
#include "tls.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cctype>

namespace browser::net::http {

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
    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key) const;
    bool has(const std::string& key) const;
    void remove(const std::string& key);
    std::string to_string() const;

    static Result<Headers> parse(const u8* data, u32 len, u32& consumed);

    const std::unordered_map<std::string, std::string>& all() const { return headers_; }

private:
    static std::string normalize_key(const std::string& key) {
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

    static Result<Response> parse(const u8* data, u32 len);
};

class HTTP1Client {
public:
    HTTP1Client();
    ~HTTP1Client();
    HTTP1Client(HTTP1Client&&) noexcept;
    HTTP1Client& operator=(HTTP1Client&&) noexcept;
    HTTP1Client(const HTTP1Client&) = delete;

    Result<void> connect(const std::string& host, u16 port, bool use_tls,
                         Connection* existing_tcp = nullptr,
                         tls::TLSConnection* existing_tls = nullptr);
    Result<Response> execute(const Request& req);
    void close();
    bool is_connected() const;

    async::task<Response> execute_async(const Request& req);

private:
    Connection tcp_;
    std::unique_ptr<tls::TLSConnection> tls_;
    bool use_tls_ = false;

    Result<void> connect_if_needed(const Request& req);
    Result<Response> read_response();
};

} // namespace browser::net::http
