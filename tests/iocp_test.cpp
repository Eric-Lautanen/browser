#include "test_framework.hpp"
#include "utility.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../net/iocp.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>

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
