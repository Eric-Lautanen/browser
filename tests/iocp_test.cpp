#include "../net/iocp.hpp"

#include "../async/executor.hpp"
#include "../net/socket/types.hpp"
#include "test_framework.hpp"
#include "utility.hpp"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace browser;
using namespace browser::net;

static WSADATA wsa_data;
static bool wsa_started = false;

static void ensure_wsa() {
    if (!wsa_started) {
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        wsa_started = true;
    }
}

TEST(iocp_create_close, {
    ensure_wsa();
    IOCP iocp;
    ASSERT(iocp.create().is_ok());
    ASSERT(iocp.is_valid());
    iocp.close();
    ASSERT(!iocp.is_valid());
})

TEST(iocp_post_get, {
    ensure_wsa();
    IOCP iocp;
    ASSERT(iocp.create().is_ok());

    OVERLAPPED ol = {};
    BOOL ok = iocp.post_status(42, 123, &ol);
    ASSERT(ok);

    ULONG_PTR key = 0;
    DWORD bytes = 0;
    OVERLAPPED* pol = nullptr;
    ok = iocp.get_status(&key, &bytes, &pol, 1000);
    ASSERT(ok);
    ASSERT(key == 42);
    ASSERT(bytes == 123);
    ASSERT(pol == &ol);
})

TEST(iocp_tcp_echo, {
    ensure_wsa();
    IOCP iocp;
    ASSERT(iocp.create().is_ok());

    SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(listen_sock != INVALID_SOCKET);
    ASSERT(iocp.associate_socket(listen_sock, 1).is_ok());

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT(::bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);

    struct sockaddr_in bound_addr = {};
    int bound_len = sizeof(bound_addr);
    ::getsockname(listen_sock, (struct sockaddr*)&bound_addr, &bound_len);
    u16 port = ntohs(bound_addr.sin_port);

    ASSERT(::listen(listen_sock, SOMAXCONN) == 0);

    std::atomic<u32> completions{0};
    std::atomic<bool> server_ready{false};

    std::thread acceptor([&]() {
        SOCKET client = ::accept(listen_sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        iocp.associate_socket(client, 2);
        server_ready.store(true, std::memory_order_release);
        char buf[1024];
        for (int i = 0; i < 100; i++) {
            int n = ::recv(client, buf, sizeof(buf), 0);
            if (n > 0) {
                ::send(client, buf, n, 0);
            }
        }
        ::closesocket(client);
    });

    SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(client != INVALID_SOCKET);
    iocp.associate_socket(client, 3);
    ::connect(client, (struct sockaddr*)&bound_addr, sizeof(bound_addr));

    const char* msg = "Hello IOCP!";
    for (int i = 0; i < 100; i++) {
        ::send(client, msg, (int)std::strlen(msg), 0);
        char buf[1024];
        int n = ::recv(client, buf, sizeof(buf), 0);
        if (n > 0) completions.fetch_add(1);
    }

    ::closesocket(client);
    acceptor.join();
    ::closesocket(listen_sock);
    (void)port;
    ASSERT(completions.load() == 100);
})

TEST(iocp_concurrent_1000, {
    ensure_wsa();
    IOCP iocp;
    ASSERT(iocp.create().is_ok());

    SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(listen_sock != INVALID_SOCKET);
    ASSERT(iocp.associate_socket(listen_sock, 1).is_ok());

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));

    struct sockaddr_in bound_addr = {};
    int bound_len = sizeof(bound_addr);
    ::getsockname(listen_sock, (struct sockaddr*)&bound_addr, &bound_len);
    u16 port = ntohs(bound_addr.sin_port);
    ::listen(listen_sock, SOMAXCONN);

    std::atomic<int> total_completions{0};
    int num_ops = 1000;

    std::thread server([&]() {
        for (int i = 0; i < num_ops; i++) {
            SOCKET client = ::accept(listen_sock, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                iocp.associate_socket(client, 2);
                char buf[64];
                int n = ::recv(client, buf, sizeof(buf), 0);
                if (n > 0) {
                    total_completions.fetch_add(1);
                }
                ::closesocket(client);
            }
        }
    });

    std::vector<std::thread> clients;
    for (int i = 0; i < num_ops; i++) {
        clients.emplace_back([&]() {
            SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET) return;
            iocp.associate_socket(s, 3);
            ::connect(s, (struct sockaddr*)&bound_addr, sizeof(bound_addr));
            const char* msg = "ping";
            ::send(s, msg, 4, 0);
            ::closesocket(s);
        });
    }

    for (auto& t : clients) t.join();
    server.join();
    ::closesocket(listen_sock);
    (void)port;
    ASSERT(total_completions.load() == num_ops);
})

// N-C4: the awaiter must register the coroutine handle into the IoOverlapped
// *before* invoking the initiation callable, so an IOCP worker can never see
// a completion packet with no resume target stored.
TEST(iocp_awaiter_registers_handle_before_initiation, {
    IoOverlapped ol;
    void *seen_at_initiation = nullptr;
    auto make_task = [&]() -> async::task<int> {
        co_await co_iocp(&ol, [&]() -> int {
            seen_at_initiation = ol.coro.load();
            return 1;  // synchronous failure: no packet will arrive
        });
        co_return 7;
    };
    auto t = make_task();
    Result<int> r = t.sync_wait();
    ASSERT(r.is_ok());
    ASSERT(r.unwrap() == 7);
    // Without the fix the init callable runs before any resume target exists.
    ASSERT(seen_at_initiation != nullptr);
})

// N-C4: when initiation fails synchronously (no packet will ever be queued)
// the coroutine must resume inline instead of suspending forever.
TEST(iocp_awaiter_immediate_failure_resumes_inline, {
    IoOverlapped ol;
    bool resumed = false;
    auto make_task = [&]() -> async::task<bool> {
        co_await co_iocp(&ol, []() -> int { return 1; });
        resumed = true;
        co_return true;
    };
    auto t = make_task();
    t.sync_wait();
    ASSERT(resumed);
    // The failed registration must not leave a stale resume target behind.
    ASSERT(ol.coro.load() == nullptr);
})

// N-C4 end-to-end: many async sends/recvs against a loopback echo server.
// Under the old initiate-then-register pattern, loopback operations complete
// immediately and workers consume packets before the handle is stored — each
// occurrence hung that coroutine permanently. This test times out / fails in
// that case instead of passing silently.
TEST(iocp_async_tcp_echo_stress_no_lost_wakeup, {
    ensure_wsa();
    SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT(listen_sock != INVALID_SOCKET);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT(::bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);

    struct sockaddr_in bound_addr = {};
    int bound_len = sizeof(bound_addr);
    ::getsockname(listen_sock, (struct sockaddr *)&bound_addr, &bound_len);
    u16 port = ntohs(bound_addr.sin_port);
    ASSERT(::listen(listen_sock, SOMAXCONN) == 0);

    constexpr int kRounds = 64;
    std::thread echo_server([&]() {
        for (int i = 0; i < kRounds; i++) {
            SOCKET c = ::accept(listen_sock, nullptr, nullptr);
            if (c == INVALID_SOCKET)
                break;
            char buf[512];
            for (;;) {
                int n = ::recv(c, buf, sizeof(buf), 0);
                if (n <= 0)
                    break;
                int off = 0;
                while (off < n) {
                    int s = ::send(c, buf + off, n - off, 0);
                    if (s <= 0)
                        break;
                    off += s;
                }
            }
            ::closesocket(c);
            break;  // single connection handles all rounds
        }
    });

    auto client_task = [&]() -> async::task<Result<bool>> {
        auto sock_r = Socket::create_tcp();
        if (sock_r.is_err())
            co_return std::string("create_tcp failed");
        auto &sock = *sock_r.unwrap();
        auto cr = co_await sock.async_connect("127.0.0.1", port);
        if (cr.is_err())
            co_return std::string("connect: ") + cr.unwrap_err();
        std::vector<u8> send_buf(256);
        std::vector<u8> recv_buf(256);
        for (int i = 0; i < kRounds; i++) {
            std::memset(send_buf.data(), 'A', send_buf.size());
            send_buf[0] = static_cast<u8>(i);
            auto sr = co_await sock.async_send_all(span<u8>(send_buf.data(), (u32)send_buf.size()));
            if (sr.is_err() || !sr.unwrap())
                co_return std::string("send_all failed");
            auto rr = co_await sock.async_recv_exact(span<u8>(recv_buf.data(), (u32)recv_buf.size()));
            if (rr.is_err())
                co_return std::string("recv_exact: ") + rr.unwrap_err();
            if (recv_buf[0] != static_cast<u8>(i) || recv_buf.back() != 'A')
                co_return std::string("data mismatch");
        }
        sock.close();
        co_return true;
    };
    auto t = client_task();
    auto outer = t.sync_wait();

    ::closesocket(listen_sock);
    echo_server.join();

    if (outer.is_err()) {
        _err = "outer err: " + outer.unwrap_err();
        return false;
    }
    Result<bool> r = std::move(outer.unwrap());
    ASSERT(r.is_ok());
    if (!r.unwrap()) {
        _err = "echo stress failed: " + r.unwrap_err();
        return false;
    }
    return true;
})
