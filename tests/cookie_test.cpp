#include "test_framework.hpp"
#include "utility.hpp"
#include "../net/cookie_jar.hpp"
#include "../net/url.hpp"

using namespace browser;
using namespace browser::net;

TEST(cookie_set_get, {
    CookieJar jar;
    Cookie c;
    c.name = "test";
    c.value = "value123";
    c.domain = "example.com";
    c.path = "/";

    jar.set_cookie("example.com", "/", c);

    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].name, "test");
    ASSERT_EQ(cookies[0].value, "value123");
})

TEST(cookie_domain_filter, {
    CookieJar jar;
    Cookie c;
    c.name = "session";
    c.value = "abc";
    c.domain = ".example.com";
    c.path = "/";
    jar.set_cookie("www.example.com", "/", c);

    // Should match subdomain
    auto cookies = jar.get_cookies("sub.example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);

    // Should not match different domain
    cookies = jar.get_cookies("other.com", "/", false);
    ASSERT_EQ(cookies.size(), 0u);
})

TEST(cookie_path_filter, {
    CookieJar jar;
    Cookie c;
    c.name = "path_cookie";
    c.value = "val";
    c.domain = "example.com";
    c.path = "/foo";
    jar.set_cookie("example.com", "/foo", c);

    auto cookies = jar.get_cookies("example.com", "/foo/bar", false);
    ASSERT_EQ(cookies.size(), 1u);

    cookies = jar.get_cookies("example.com", "/other", false);
    ASSERT_EQ(cookies.size(), 0u);
})

TEST(cookie_secure_flag, {
    CookieJar jar;
    Cookie c;
    c.name = "secure_cookie";
    c.value = "secret";
    c.domain = "example.com";
    c.path = "/";
    c.secure = true;
    jar.set_cookie("example.com", "/", c);

    // Not sent over non-secure
    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 0u);

    // Sent over secure
    cookies = jar.get_cookies("example.com", "/", true);
    ASSERT_EQ(cookies.size(), 1u);
})

TEST(cookie_httpOnly_flag, {
    CookieJar jar;
    Cookie c;
    c.name = "http_only";
    c.value = "hidden";
    c.domain = "example.com";
    c.path = "/";
    c.httpOnly = true;
    jar.set_cookie("example.com", "/", c);

    // Not visible to JS
    auto js_cookies = jar.get_cookies_for_js("example.com", "/", false);
    ASSERT_EQ(js_cookies.size(), 0u);

    // Visible to HTTP
    auto http_cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(http_cookies.size(), 1u);
})

TEST(cookie_expiration, {
    CookieJar jar;
    Cookie c;
    c.name = "temp";
    c.value = "expires_soon";
    c.domain = "example.com";
    c.path = "/";
    c.expires_time = 1; // expired long ago
    jar.set_cookie("example.com", "/", c);

    ASSERT(jar.is_expired(c));

    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 0u);
})

TEST(cookie_clear_expired, {
    CookieJar jar;
    Cookie c;
    c.name = "expired_cookie";
    c.value = "gone";
    c.domain = "example.com";
    c.path = "/";
    c.expires_time = 1;
    jar.set_cookie("example.com", "/", c);

    jar.clear_expired();
    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 0u);
})

TEST(cookie_file_roundtrip, {
    CookieJar jar;
    Cookie c;
    c.name = "persistent";
    c.value = "stored";
    c.domain = "example.com";
    c.path = "/";
    c.secure = false;
    c.expires_time = 9999999999ULL;
    jar.set_cookie("example.com", "/", c);

    auto sr = jar.save_to_file("test_cookies.txt");
    ASSERT(sr.is_ok());

    CookieJar jar2;
    auto lr = jar2.load_from_file("test_cookies.txt");
    ASSERT(lr.is_ok());

    auto cookies = jar2.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].name, "persistent");
    ASSERT_EQ(cookies[0].value, "stored");

    std::remove("test_cookies.txt");
})

TEST(cookie_same_site, {
    CookieJar jar;
    Cookie c;
    c.name = "samesite_test";
    c.value = "val";
    c.domain = "example.com";
    c.path = "/";
    c.sameSite = "lax";
    jar.set_cookie("example.com", "/", c);

    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].sameSite, "lax");
})

TEST(cookie_multiple_domains, {
    CookieJar jar;
    for (auto& domain : {"example.com", "test.org", "other.net"}) {
        Cookie c;
        c.name = "site";
        c.value = std::string(domain);
        c.domain = domain;
        c.path = "/";
        jar.set_cookie(domain, "/", c);
    }

    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].value, "example.com");

    cookies = jar.get_cookies("test.org", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].value, "test.org");
})

TEST(cookie_overwrite, {
    CookieJar jar;
    Cookie c;
    c.name = "overwrite";
    c.value = "old";
    c.domain = "example.com";
    c.path = "/";
    jar.set_cookie("example.com", "/", c);

    c.value = "new";
    jar.set_cookie("example.com", "/", c);

    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 1u);
    ASSERT_EQ(cookies[0].value, "new");
})

TEST(cookie_clear, {
    CookieJar jar;
    Cookie c;
    c.name = "clearme";
    c.value = "bye";
    c.domain = "example.com";
    c.path = "/";
    jar.set_cookie("example.com", "/", c);

    jar.clear();
    auto cookies = jar.get_cookies("example.com", "/", false);
    ASSERT_EQ(cookies.size(), 0u);
})
