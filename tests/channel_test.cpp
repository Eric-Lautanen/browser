#include "test_framework.hpp"
#include "utility.hpp"
#include "../async/channel.hpp"
#include "../async/thread_pool.hpp"
#include <thread>
#include <atomic>
#include <string>

using namespace browser;
using namespace browser::async;

TEST(channel_try_send_receive, {
    channel<int> ch;
    bool ok = ch.try_send(42);
    ASSERT(ok);
    auto val = ch.try_receive();
    ASSERT(val.has_value());
    ASSERT(*val == 42);
})

TEST(channel_try_receive_empty, {
    channel<int> ch;
    auto val = ch.try_receive();
    ASSERT(!val.has_value());
})

TEST(channel_fifo_order, {
    channel<int> ch;
    ch.try_send(1);
    ch.try_send(2);
    ch.try_send(3);
    auto v1 = ch.try_receive();
    auto v2 = ch.try_receive();
    auto v3 = ch.try_receive();
    ASSERT(v1.has_value() && *v1 == 1);
    ASSERT(v2.has_value() && *v2 == 2);
    ASSERT(v3.has_value() && *v3 == 3);
})

TEST(channel_multi_thread_ping_pong, {
    channel<int> ch1;
    channel<int> ch2;
    std::atomic<int> total{0};
    int count = 1000;

    std::thread t1([&]() {
        for (int i = 0; i < count; i++) {
            ch1.send(i);
            auto val = ch2.receive();
            total.fetch_add(val);
        }
    });

    std::thread t2([&]() {
        for (int i = 0; i < count; i++) {
            auto val = ch1.receive();
            ch2.send(val + 1);
        }
    });

    t1.join();
    t2.join();
    int expected = 0;
    for (int i = 0; i < count; i++) {
        expected += i + 1;
    }
    ASSERT(total.load() == expected);
})

TEST(channel_bounded_backpressure, {
    channel<int> ch(5);
    for (int i = 0; i < 5; i++) {
        bool ok = ch.try_send(i);
        ASSERT(ok);
    }
    bool overflow = ch.try_send(99);
    ASSERT(!overflow);
    ASSERT(ch.size() == 5);
})

TEST(channel_blocking_send_receive, {
    channel<int> ch(1);
    std::thread sender([&]() {
        ch.send(10);
        ch.send(20);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto v1 = ch.try_receive();
    ASSERT(v1.has_value() && *v1 == 10);
    auto v2 = ch.try_receive();
    ASSERT(v2.has_value() && *v2 == 20);
    sender.join();
})

TEST(channel_string_transfer, {
    channel<std::string> ch;
    ch.send(std::string("hello world"));
    auto val = ch.receive();
    ASSERT(val == "hello world");
})

TEST(channel_close, {
    channel<int> ch;
    ch.send(1);
    ch.send(2);
    ch.close();
    ASSERT(ch.is_closed());
    auto v1 = ch.receive();
    ASSERT(v1 == 1);
    auto v2 = ch.receive();
    ASSERT(v2 == 2);
})

TEST(channel_empty_size, {
    channel<int> ch;
    ASSERT(ch.empty());
    ASSERT(ch.size() == 0);
    ch.send(1);
    ASSERT(!ch.empty());
    ASSERT(ch.size() == 1);
})
