#include "test_framework.hpp"
#include "utility.hpp"
#include "../net/http_cache.hpp"
#include "../net/http.hpp"
#include <cstring>
#include <string>

using namespace browser;
using namespace browser::net::cache;
using namespace browser::net::http;

TEST(cache_init, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());
})

TEST(cache_store_lookup, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    Response resp;
    resp.status.code = 200;
    resp.status.reason = "OK";
    resp.http_version = "HTTP/1.1";
    resp.headers.set("Content-Type", "text/html");
    resp.body = { 'H', 'e', 'l', 'l', 'o' };

    auto sr = cache.store("GET", "http://example.com/", resp);
    ASSERT(sr.is_ok());

    auto lr = cache.lookup("GET", "http://example.com/");
    ASSERT(lr.is_ok());
    auto entry = lr.unwrap();
    ASSERT(entry.status_code == 200);
    ASSERT(entry.body.size() == 5);
    ASSERT(entry.headers.get("content-type") == "text/html");
})

TEST(cache_miss, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    auto lr = cache.lookup("GET", "http://nonexistent.com/");
    ASSERT(lr.is_err());
})

TEST(cache_expiration, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    Response resp;
    resp.status.code = 200;
    resp.status.reason = "OK";
    resp.http_version = "HTTP/1.1";
    resp.body = { 'd', 'a', 't', 'a' };
    resp.headers.set("Cache-Control", "max-age=0");

    ASSERT(cache.store("GET", "http://test.com/", resp).is_ok());

    auto lr = cache.lookup("GET", "http://test.com/");
    ASSERT(lr.is_ok());
    auto entry = lr.unwrap();
    ASSERT(!HTTPCache::is_fresh(entry));
})

TEST(cache_fresh, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    Response resp;
    resp.status.code = 200;
    resp.status.reason = "OK";
    resp.http_version = "HTTP/1.1";
    resp.body = { 'd', 'a', 't', 'a' };
    resp.headers.set("Cache-Control", "max-age=3600");

    ASSERT(cache.store("GET", "http://test.com/fresh", resp).is_ok());
    auto lr = cache.lookup("GET", "http://test.com/fresh");
    ASSERT(lr.is_ok());
    auto entry = lr.unwrap();
    ASSERT(HTTPCache::is_fresh(entry));
})

TEST(cache_conditional_headers, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    Response resp;
    resp.status.code = 200;
    resp.status.reason = "OK";
    resp.http_version = "HTTP/1.1";
    resp.headers.set("ETag", "\"abc123\"");
    resp.headers.set("Last-Modified", "Mon, 01 Jan 2024 00:00:00 GMT");

    ASSERT(cache.store("GET", "http://test.com/etag", resp).is_ok());
    auto lr = cache.lookup("GET", "http://test.com/etag");
    ASSERT(lr.is_ok());
    auto entry = lr.unwrap();

    Request req;
    req.method = Method::GET;
    HTTPCache::add_conditional_headers(entry, req);
    ASSERT(req.headers.has("If-None-Match"));
    ASSERT(req.headers.get("If-None-Match") == "\"abc123\"");
    ASSERT(req.headers.has("If-Modified-Since"));
})

TEST(cache_multiple_entries, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    for (int i = 0; i < 5; i++) {
        Response resp;
        resp.status.code = 200;
        resp.http_version = "HTTP/1.1";
        std::string url = "http://test.com/page" + std::to_string(i);
        resp.headers.set("Cache-Control", "max-age=100");
        ASSERT(cache.store("GET", url, resp).is_ok());
    }

    for (int i = 0; i < 5; i++) {
        std::string url = "http://test.com/page" + std::to_string(i);
        auto lr = cache.lookup("GET", url);
        ASSERT(lr.is_ok());
    }
})

TEST(cache_clear, {
    HTTPCache cache;
    ASSERT(cache.init("./test_cache/").is_ok());
    ASSERT(cache.clear().is_ok());

    Response resp;
    resp.status.code = 200;
    resp.http_version = "HTTP/1.1";
    ASSERT(cache.store("GET", "http://test.com/clearable", resp).is_ok());
    ASSERT(cache.lookup("GET", "http://test.com/clearable").is_ok());
    ASSERT(cache.clear().is_ok());
    ASSERT(cache.lookup("GET", "http://test.com/clearable").is_err());
})
