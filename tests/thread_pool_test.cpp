#include "test_framework.hpp"
#include "utility.hpp"
#include "../async/thread_pool.hpp"
#include "../async/mutex.hpp"
#include "../async/scoped_lock.hpp"
#include <atomic>
#include <vector>
#include <set>
#include <thread>

using namespace browser;
using namespace browser::async;

TEST(thread_pool_basic_enqueue, {
    thread_pool pool;
    ASSERT(pool.is_valid());
    std::atomic<int> counter{0};
    pool.enqueue([&]() { counter.fetch_add(1); });
    pool.enqueue([&]() { counter.fetch_add(1); });
    pool.enqueue([&]() { counter.fetch_add(1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT(counter.load() == 3);
})

TEST(thread_pool_10k_items, {
    thread_pool pool;
    ASSERT(pool.is_valid());
    std::atomic<int64_t> counter{0};
    int num_items = 10000;
    for (int i = 0; i < num_items; i++) {
        pool.enqueue([&counter]() {
            counter.fetch_add(1);
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ASSERT(counter.load() == num_items);
})

TEST(thread_pool_no_races, {
    thread_pool pool;
    ASSERT(pool.is_valid());
    mutex mtx;
    std::set<int> seen;
    int num_items = 5000;
    std::atomic<int> duplicates{0};

    for (int i = 0; i < num_items; i++) {
        pool.enqueue([&mtx, &seen, &duplicates, i]() {
            scoped_lock lock(mtx);
            if (seen.find(i) != seen.end()) {
                duplicates.fetch_add(1);
            }
            seen.insert(i);
        });
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT(duplicates.load() == 0);
    ASSERT(static_cast<int>(seen.size()) == num_items);
})

TEST(thread_pool_many_threads, {
    thread_pool pool(32);
    ASSERT(pool.is_valid());
    std::atomic<int> counter{0};
    int num_items = 2000;
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&pool, &counter, num_items]() {
            for (int i = 0; i < num_items / 8; i++) {
                pool.enqueue([&counter]() {
                    counter.fetch_add(1);
                });
            }
        });
    }
    for (auto& t : threads) t.join();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT(counter.load() == num_items);
})

TEST(thread_pool_return_values, {
    thread_pool pool;
    ASSERT(pool.is_valid());
    std::atomic<int> sum{0};
    for (int i = 0; i < 100; i++) {
        pool.enqueue([&sum, i]() {
            sum.fetch_add(i);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int expected = 0;
    for (int i = 0; i < 100; i++) expected += i;
    ASSERT(sum.load() == expected);
})
