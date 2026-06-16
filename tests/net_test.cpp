#include "test_framework.hpp"
#include "utility.hpp"
#include "../net/socket.hpp"
#include "../net/dns.hpp"
#include "../net/connection.hpp"
#include "../net/url.hpp"
#include "../net/http.hpp"
#include "../net/http2.hpp"
#include "../net/http_client.hpp"
#include <cstring>
#include <vector>

static const browser::u8 expected_basic[] = {3, 'w', 'w', 'w', 6, 'g', 'o', 'o', 'g', 'l', 'e', 3, 'c', 'o', 'm', 0};
static const browser::u8 expected_single[] = {9, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0};

TEST(ipv4_address, {
    auto a = browser::net::IPv4Address::from_string("192.168.1.1");
    ASSERT(a.is_ok());
    auto addr = a.unwrap();
    ASSERT(addr.octets[0] == 192);
    ASSERT(addr.octets[1] == 168);
    ASSERT(addr.octets[2] == 1);
    ASSERT(addr.octets[3] == 1);
    ASSERT(addr.to_string() == "192.168.1.1");
})

TEST(ipv4_address_zero, {
    auto a = browser::net::IPv4Address::from_string("0.0.0.0");
    ASSERT(a.is_ok());
    ASSERT(a.unwrap().to_string() == "0.0.0.0");
})

TEST(ipv4_address_invalid, {
    auto a = browser::net::IPv4Address::from_string("not an ip");
    ASSERT(a.is_err());
})

TEST(ipv4_address_eq, {
    auto a1 = browser::net::IPv4Address(10, 0, 0, 1);
    auto a2 = browser::net::IPv4Address(10, 0, 0, 1);
    auto a3 = browser::net::IPv4Address(10, 0, 0, 2);
    ASSERT(a1 == a2);
    ASSERT(a1 != a3);
})

TEST(ipv6_address_double_colon, {
    auto a = browser::net::IPv6Address::from_string("2001:db8::1");
    ASSERT(a.is_ok());
    auto addr = a.unwrap();
    ASSERT(addr.groups[0] == 0x2001);
    ASSERT(addr.groups[1] == 0x0db8);
    ASSERT(addr.groups[6] == 0);
    ASSERT(addr.groups[7] == 1);
})

TEST(ipv6_address_loopback, {
    auto a = browser::net::IPv6Address::from_string("::1");
    ASSERT(a.is_ok());
    auto addr = a.unwrap();
    ASSERT(addr.groups[0] == 0);
    ASSERT(addr.groups[7] == 1);
})

TEST(ipv6_address_full, {
    auto a = browser::net::IPv6Address::from_string("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    ASSERT(a.is_ok());
    auto addr = a.unwrap();
    ASSERT(addr.groups[0] == 0x2001);
    ASSERT(addr.groups[1] == 0x0db8);
    ASSERT(addr.groups[4] == 0);
    ASSERT(addr.groups[5] == 0x8a2e);
})

TEST(socket_create, {
    auto r = browser::net::Socket::create_tcp();
    ASSERT(r.is_ok());
})

TEST(udp_socket_create, {
    auto r = browser::net::UDPSocket::create();
    ASSERT(r.is_ok());
})

TEST(dns_encode_basic, {
    auto enc = browser::net::DNSResolver::encode_name("www.google.com");
    ASSERT(enc.size() == 16);
    for (std::size_t i = 0; i < enc.size(); i++) {
        ASSERT(enc[i] == expected_basic[i]);
    }
})

TEST(dns_encode_single, {
    auto enc = browser::net::DNSResolver::encode_name("localhost");
    ASSERT(enc.size() == 11);
    for (std::size_t i = 0; i < enc.size(); i++) {
        ASSERT(enc[i] == expected_single[i]);
    }
})

TEST(dns_encode_root, {
    auto enc = browser::net::DNSResolver::encode_name("");
    ASSERT(enc.size() == 1);
    ASSERT(enc[0] == 0);
})

TEST(connection_create_close, {
    browser::net::Connection conn;
    ASSERT(!conn.is_open());
    conn.close();
    ASSERT(!conn.is_open());
})

TEST(connection_send_no_open, {
    browser::net::Connection conn;
    browser::u8 buf[4];
    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 4;
    auto r = conn.send(buf, 4);
    ASSERT(r.is_err());
})

TEST(connection_receive_until_close_no_open, {
    browser::net::Connection conn;
    auto r = conn.receive_until_close();
    ASSERT(r.is_err());
})

TEST(dns_resolve_google, {
    browser::net::DNSResolver resolver;
    auto task = resolver.resolve_a("www.google.com");
    auto result = task.sync_wait();
    if (result.is_err()) return true;
    auto addrs = result.unwrap();
    ASSERT(addrs.size() > 0);
    for (auto& addr : addrs) {
        ASSERT(addr.octets[0] != 0 || addr.octets[1] != 0 ||
               addr.octets[2] != 0 || addr.octets[3] != 0);
    }
})

TEST(dns_resolve_localhost, {
    browser::net::DNSResolver resolver;
    auto task = resolver.resolve_a("localhost");
    auto result = task.sync_wait();
    if (result.is_err()) return true;
    auto addrs = result.unwrap();
    ASSERT(addrs.size() > 0);
})

TEST(connection_http, {
    browser::net::Connection conn;
    browser::net::ConnectionConfig cfg;
    cfg.read_timeout_ms = 15000;
    auto r = conn.open("httpbin.org", 80, cfg);
    if (r.is_err()) return true;
    ASSERT(conn.is_open());
    const char* req = "GET /get HTTP/1.1\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n";
    auto req_len = static_cast<browser::u32>(std::strlen(req));
    auto sr = conn.send_all((const browser::u8*)(req), req_len);
    ASSERT(sr.is_ok());
    auto resp = conn.receive_until_close();
    ASSERT(resp.is_ok());
    ASSERT(resp.unwrap().size() > 0);
    conn.close();
    ASSERT(!conn.is_open());
})

TEST(connection_move, {
    browser::net::Connection conn;
    browser::net::ConnectionConfig cfg;
    cfg.read_timeout_ms = 15000;
    auto r = conn.open("example.com", 80, cfg);
    if (r.is_err()) return true;
    ASSERT(conn.is_open());
    browser::net::Connection conn2(std::move(conn));
    ASSERT(!conn.is_open());
    ASSERT(conn2.is_open());
    ASSERT(conn2.host() == "example.com");
    ASSERT(conn2.port() == 80);
    ASSERT(conn2.socket() != nullptr);
    conn2.close();
})

TEST(connection_move_assign, {
    browser::net::Connection conn;
    auto r = conn.open("example.com", 80);
    if (r.is_err()) return true;
    browser::net::Connection conn2;
    conn2 = std::move(conn);
    ASSERT(!conn.is_open());
    ASSERT(conn2.is_open());
    conn2.close();
})

TEST(dns_set_server, {
    browser::net::DNSResolver resolver;
    resolver.set_dns_server(browser::net::IPv4Address(1, 1, 1, 1));
    auto task = resolver.resolve_a("example.com");
    auto result = task.sync_wait();
    if (result.is_err()) return true;
    auto addrs = result.unwrap();
    ASSERT(addrs.size() > 0);
})

TEST(connection_socket_accessor, {
    browser::net::Connection conn;
    auto r = conn.open("example.com", 80);
    if (r.is_err()) return true;
    ASSERT(conn.socket() != nullptr);
    conn.close();
    ASSERT(conn.socket() == nullptr);
})

TEST(connection_http_example, {
    browser::net::Connection conn;
    browser::net::ConnectionConfig cfg;
    cfg.read_timeout_ms = 15000;
    auto r = conn.open("example.com", 80, cfg);
    if (r.is_err()) return true;
    ASSERT(conn.is_open());
    const char* req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    auto req_len = static_cast<browser::u32>(std::strlen(req));
    auto sr = conn.send_all((const browser::u8*)(req), req_len);
    ASSERT(sr.is_ok());
    auto resp = conn.receive_until_close();
    ASSERT(resp.is_ok());
    ASSERT(resp.unwrap().size() > 0);
    ASSERT(resp.unwrap().size() > 100);
    conn.close();
})

// --- URL tests ---

TEST(url_parse_http, {
    auto r = browser::net::URL::parse("http://example.com/path");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.scheme, "http");
    ASSERT_EQ(u.host, "example.com");
    ASSERT_EQ(u.path, "/path");
    ASSERT_EQ(u.port, 80);
})

TEST(url_parse_https, {
    auto r = browser::net::URL::parse("https://google.com/search?q=hello");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.scheme, "https");
    ASSERT_EQ(u.host, "google.com");
    ASSERT_EQ(u.path, "/search");
    ASSERT_EQ(u.query, "q=hello");
    ASSERT_EQ(u.port, 443);
})

TEST(url_parse_port, {
    auto r = browser::net::URL::parse("http://localhost:8080/test");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.host, "localhost");
    ASSERT_EQ(u.port, 8080);
    ASSERT_EQ(u.path, "/test");
})

TEST(url_parse_fragment, {
    auto r = browser::net::URL::parse("https://example.com/page#section");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.fragment, "section");
})

TEST(url_parse_userinfo, {
    auto r = browser::net::URL::parse("https://user:pass@host.com/");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.username, "user");
    ASSERT_EQ(u.password, "pass");
    ASSERT_EQ(u.host, "host.com");
})

TEST(url_parse_no_path, {
    auto r = browser::net::URL::parse("http://example.com");
    ASSERT(r.is_ok());
    auto u = r.unwrap();
    ASSERT_EQ(u.host, "example.com");
    ASSERT_EQ(u.port, 80);
})

TEST(url_to_string, {
    auto r = browser::net::URL::parse("https://google.com/search?q=hello");
    ASSERT(r.is_ok());
    auto s = r.unwrap().to_string();
    ASSERT_EQ(s, "https://google.com/search?q=hello");
})

TEST(url_resolve_absolute, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("http://other.com/");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().host, "other.com");
})

TEST(url_resolve_relative, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("c/d");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().path, "/a/c/d");
})

TEST(url_resolve_dotdot, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b/c");
    ASSERT(r.is_ok());
    base = r.unwrap();
    // RFC 3986 §5.2.4: ../../d from /a/b/c -> /d
    auto resolved = base.resolve("../../d");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().path, "/d");
})

TEST(url_resolve_dotdot_mid, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b/c");
    ASSERT(r.is_ok());
    base = r.unwrap();
    // RFC 3986 §5.2.4: ../x/../y from /a/b/c -> /a/b/y (one up, then back into, then up again)
    auto resolved = base.resolve("../x/../y");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().path, "/a/y");
})

TEST(url_resolve_abs_path, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("/other");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().path, "/other");
})

TEST(url_resolve_scheme_relative, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("//other.com/foo");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().host, "other.com");
    ASSERT_EQ(resolved.unwrap().scheme, "http");
})

TEST(url_resolve_query, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/page");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("?query=1");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().query, "query=1");
})

TEST(url_resolve_fragment, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/page");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("#sec");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().fragment, "sec");
})

TEST(url_resolve_rooted_parent, {
    browser::net::URL base;
    auto r = browser::net::URL::parse("http://example.com/a/b/c");
    ASSERT(r.is_ok());
    base = r.unwrap();
    auto resolved = base.resolve("/../../d");
    ASSERT(resolved.is_ok());
    ASSERT_EQ(resolved.unwrap().path, "/d");
})

TEST(url_encode_decode, {
    auto encoded = browser::net::url_encode("hello world");
    ASSERT_EQ(encoded, "hello%20world");
    auto decoded = browser::net::url_decode(encoded);
    ASSERT_EQ(decoded, "hello world");
})

// --- HTTP tests ---

TEST(http_parse_response_simple, {
    auto raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
    auto r = browser::net::http::Response::parse((const browser::u8*)raw, (browser::u32)std::strlen(raw));
    ASSERT(r.is_ok());
    auto resp = r.unwrap();
    ASSERT_EQ(resp.status.code, 200);
    ASSERT_EQ(resp.status.reason, "OK");
    ASSERT_EQ(resp.http_version, "HTTP/1.1");
    ASSERT_EQ(resp.body.size(), 5u);
    ASSERT_EQ(resp.body[0], 'H');
})

TEST(http_parse_response_chunked, {
    auto raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHello\r\n0\r\n\r\n";
    auto r = browser::net::http::Response::parse((const browser::u8*)raw, (browser::u32)std::strlen(raw));
    ASSERT(r.is_ok());
    auto resp = r.unwrap();
    ASSERT_EQ(resp.status.code, 200);
    ASSERT_EQ(resp.body.size(), 5u);
    ASSERT_EQ(std::string(resp.body.begin(), resp.body.end()), "Hello");
})

TEST(http_parse_response_no_body, {
    auto raw = "HTTP/1.1 204 No Content\r\n\r\n";
    auto r = browser::net::http::Response::parse((const browser::u8*)raw, (browser::u32)std::strlen(raw));
    ASSERT(r.is_ok());
    ASSERT_EQ(r.unwrap().body.size(), 0u);
})

TEST(http_parse_response_headers, {
    auto raw = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nServer: test\r\nContent-Length: 0\r\n\r\n";
    auto r = browser::net::http::Response::parse((const browser::u8*)raw, (browser::u32)std::strlen(raw));
    ASSERT(r.is_ok());
    auto resp = r.unwrap();
    ASSERT_EQ(resp.headers.get("content-type"), "text/html");
    ASSERT_EQ(resp.headers.get("server"), "test");
})

TEST(http_get_example, {
    browser::net::http::HTTP1Client client;
    browser::net::http::Request req;
    req.method = browser::net::http::Method::GET;
    auto url_r = browser::net::URL::parse("http://example.com/");
    if (url_r.is_err()) return true;
    req.url = url_r.unwrap();
    req.headers.set("Host", "example.com");
    req.headers.set("Connection", "close");
    auto resp = client.execute(req);
    if (resp.is_err()) return true;
    ASSERT_EQ(resp.unwrap().status.code, 200);
    ASSERT(resp.unwrap().body.size() > 0);
})

// --- HTTP/2 unit tests ---

TEST(hpack_integer_roundtrip, {
    using namespace browser::net::http2;
    HPack hp;
    auto enc = hp.encode_integer(10, 5);
    browser::u32 pos = 0;
    browser::u32 val = hp.decode_integer(enc.data(), (browser::u32)enc.size(), pos, 5);
    ASSERT_EQ(val, 10u);
})

TEST(hpack_integer_large, {
    using namespace browser::net::http2;
    HPack hp;
    auto enc = hp.encode_integer(1337, 5);
    browser::u32 pos = 0;
    browser::u32 val = hp.decode_integer(enc.data(), (browser::u32)enc.size(), pos, 5);
    ASSERT_EQ(val, 1337u);
})

TEST(hpack_string_roundtrip, {
    using namespace browser::net::http2;
    HPack hp;
    std::string original = "test-header-value";
    auto enc = hp.encode_string(original);
    browser::u32 pos = 0;
    std::string decoded = hp.decode_string(enc.data(), (browser::u32)enc.size(), pos);
    ASSERT_EQ(decoded, original);
})

TEST(hpack_encode_decode, {
    using namespace browser::net::http2;
    HPack hp;
    std::vector<HPackEntry> headers;
    headers.push_back({":method", "GET"});
    headers.push_back({":scheme", "https"});
    headers.push_back({":path", "/"});
    headers.push_back({":authority", "example.com"});
    headers.push_back({"accept", "*/*"});
    auto encoded = hp.encode(headers);
    auto decoded = hp.decode(encoded.data(), (browser::u32)encoded.size());
    ASSERT_EQ(decoded.size(), headers.size());
    for (std::size_t i = 0; i < decoded.size(); i++) {
        ASSERT_EQ(decoded[i].name, headers[i].name);
        ASSERT_EQ(decoded[i].value, headers[i].value);
    }
})

TEST(hpack_static_table, {
    using namespace browser::net::http2;
    HPack hp;
    auto entry = hp.get_entry(2);
    ASSERT(entry != nullptr);
    ASSERT_EQ(entry->name, ":method");
    ASSERT_EQ(entry->value, "GET");
    entry = hp.get_entry(6);
    ASSERT(entry != nullptr);
    ASSERT_EQ(entry->name, ":scheme");
    ASSERT_EQ(entry->value, "http");
})

TEST(frame_header_serialize, {
    using namespace browser::net::http2;
    FrameHeader hdr;
    hdr.length = 1024;
    hdr.type = HEADERS;
    hdr.flags = 0x05;
    hdr.stream_id = 1;
    auto frame = HTTP2Client::serialize_frame(hdr, nullptr);
    ASSERT_EQ(frame.size(), 9u);
    ASSERT_EQ(frame[0], 0x00);
    ASSERT_EQ(frame[1], 0x04);
    ASSERT_EQ(frame[2], 0x00);
    ASSERT_EQ(frame[3], 1);
    ASSERT_EQ(frame[4], 0x05);
    ASSERT_EQ(frame[5], 0x00);
    ASSERT_EQ(frame[6], 0x00);
    ASSERT_EQ(frame[7], 0x00);
    ASSERT_EQ(frame[8], 0x01);
    browser::u32 pos = 0;
    auto parsed = HTTP2Client::parse_frame_header(frame.data(), 9, pos);
    ASSERT(parsed.is_ok());
    auto fh = parsed.unwrap();
    ASSERT_EQ(fh.length, 1024u);
    ASSERT_EQ(fh.type, HEADERS);
    ASSERT_EQ(fh.flags, 0x05u);
    ASSERT_EQ(fh.stream_id, 1u);
})

TEST(http2_hpack_roundtrip, {
    using namespace browser::net::http2;
    HPack hp;
    std::vector<HPackEntry> original;
    original.push_back({":status", "200"});
    original.push_back({"content-type", "text/html"});
    original.push_back({"content-length", "42"});
    original.push_back({"server", "test"});
    auto enc = hp.encode(original);
    auto dec = hp.decode(enc.data(), (browser::u32)enc.size());
    ASSERT_EQ(dec.size(), original.size());
    for (std::size_t i = 0; i < dec.size(); i++) {
        ASSERT_EQ(dec[i].name, original[i].name);
        ASSERT_EQ(dec[i].value, original[i].value);
    }
})

TEST(http2_connect_google, {
    browser::net::http2::HTTP2Client client;
    auto r = client.connect("google.com", 443, true);
    // This may fail if network is unavailable or server doesn't support h2
    if (r.is_err()) return true;
    ASSERT(client.is_connected());
    // Send GET request
    browser::net::http::Request req;
    req.method = browser::net::http::Method::GET;
    auto url_r = browser::net::URL::parse("https://google.com/");
    if (url_r.is_err()) return true;
    req.url = url_r.unwrap();
    auto resp = client.execute(req);
    if (resp.is_err()) return true;
    ASSERT_EQ(resp.unwrap().status.code, 200);
    ASSERT(resp.unwrap().body.size() > 0);
    client.close();
})

TEST(http_client_get_example, {
    browser::net::HTTPClient client;
    auto r = client.get("http://example.com/");
    if (r.is_err()) return true;
    ASSERT_EQ(r.unwrap().status.code, 200);
})

TEST(http_client_get_https, {
    browser::net::HTTPClient client;
    auto r = client.get("https://example.com/");
    if (r.is_err()) return true;
    ASSERT_EQ(r.unwrap().status.code, 200);
})

TEST(hpack_integer_boundaries, {
    using namespace browser::net::http2;
    HPack hp;
    // Test value 0
    auto enc0 = hp.encode_integer(0, 5);
    browser::u32 pos0 = 0;
    ASSERT_EQ(hp.decode_integer(enc0.data(), (browser::u32)enc0.size(), pos0, 5), 0u);
    // Test value 31 (2^5 - 1) — should need extended encoding
    auto enc31 = hp.encode_integer(31, 5);
    browser::u32 pos31 = 0;
    ASSERT_EQ(hp.decode_integer(enc31.data(), (browser::u32)enc31.size(), pos31, 5), 31u);
    // Test value 127
    auto enc127 = hp.encode_integer(127, 7);
    browser::u32 pos127 = 0;
    ASSERT_EQ(hp.decode_integer(enc127.data(), (browser::u32)enc127.size(), pos127, 7), 127u);
    // Test value 1 in 1-bit prefix (max representable single-byte = 1)
    auto enc1 = hp.encode_integer(1, 1);
    browser::u32 pos1 = 0;
    ASSERT_EQ(hp.decode_integer(enc1.data(), (browser::u32)enc1.size(), pos1, 1), 1u);
})

TEST(hpack_decode_literal_without_indexing, {
    using namespace browser::net::http2;
    // Manually build a "literal without indexing" entry (0000xxxx)
    // 0000 0000 = 0x00: type=literal-without-indexing, name index=0 (new name)
    // Then string for name, then string for value
    std::vector<browser::u8> data;
    data.push_back(0x00);
    auto name_enc = HPack::encode_string("custom-header");
    data.insert(data.end(), name_enc.begin(), name_enc.end());
    auto val_enc = HPack::encode_string("custom-value");
    data.insert(data.end(), val_enc.begin(), val_enc.end());

    HPack hp;
    auto entries = hp.decode(data.data(), (browser::u32)data.size());
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].name, "custom-header");
    ASSERT_EQ(entries[0].value, "custom-value");
})

TEST(hpack_decode_table_size_update, {
    using namespace browser::net::http2;
    HPack hp;
    // Use encode_integer to get correct encoding: 001 (3-bit prefix) + 5-bit value
    auto enc = HPack::encode_integer(200, 5);
    // Prepend the 001 prefix bits (first byte should be 0x20 | enc[0] for value < 31,
    // or 0x3F for value >= 31, with enc[0] being 31 = 0x1F)
    std::vector<browser::u8> data;
    data.push_back(0x20 | enc[0]); // 001 + 11111 = 0x3F
    for (std::size_t i = 1; i < enc.size(); i++)
        data.push_back(enc[i]);

    auto entries = hp.decode(data.data(), (browser::u32)data.size());
    ASSERT_EQ(entries.size(), 0u);
    ASSERT_EQ(hp.max_table_size(), 200u);
})
