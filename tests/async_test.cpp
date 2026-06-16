#include "test_framework.hpp"
#include "utility.hpp"
#include "../async/task.hpp"
#include <string>
#include <vector>

using namespace browser;
using namespace browser::async;

TEST(task_void_lifecycle, {
    auto make_task = []() -> task<void> {
        co_return;
    };
    auto t = make_task();
    ASSERT(!t.is_done());
    t.start();
    ASSERT(t.is_done());
})

TEST(task_int_return, {
    auto make_task = []() -> task<int> {
        co_return 42;
    };
    auto t = make_task();
    ASSERT(!t.is_done());
    auto result = t.sync_wait();
    ASSERT(result.is_ok());
    ASSERT(result.unwrap() == 42);
})

TEST(task_error_propagation, {
    auto make_task = []() -> task<int> {
        co_return Result<int>(std::string("something went wrong"));
    };
    auto t = make_task();
    auto result = t.sync_wait();
    ASSERT(result.is_err());
    ASSERT(result.unwrap_err() == "something went wrong");
})

TEST(task_chain, {
    auto inner = []() -> task<int> {
        co_return 7;
    };
    auto outer = [&]() -> task<int> {
        auto inner_task = inner();
        auto val = co_await inner_task;
        if (val.is_err()) co_return val.unwrap_err();
        co_return val.unwrap() * 2;
    };
    auto t = outer();
    auto result = t.sync_wait();
    ASSERT(result.is_ok());
    ASSERT(result.unwrap() == 14);
})

TEST(task_chain_error_propagation, {
    auto inner = []() -> task<int> {
        co_return Result<int>(std::string("fail"));
    };
    auto outer = [&]() -> task<int> {
        auto inner_task = inner();
        auto val = co_await inner_task;
        if (val.is_err()) co_return val.unwrap_err();
        co_return val.unwrap() * 2;
    };
    auto t = outer();
    auto result = t.sync_wait();
    ASSERT(result.is_err());
    ASSERT(result.unwrap_err() == "fail");
})

TEST(task_move, {
    auto make_task = []() -> task<int> {
        co_return 99;
    };
    auto t1 = make_task();
    auto t2 = std::move(t1);
    ASSERT(!t2.is_done());
    auto result = t2.sync_wait();
    ASSERT(result.is_ok());
    ASSERT(result.unwrap() == 99);
})

TEST(task_multiple_sync_wait, {
    auto t1 = []() -> task<int> { co_return 1; }();
    auto t2 = []() -> task<int> { co_return 2; }();
    auto t3 = []() -> task<int> { co_return 3; }();

    auto r1 = t1.sync_wait();
    auto r2 = t2.sync_wait();
    auto r3 = t3.sync_wait();

    ASSERT(r1.is_ok() && r1.unwrap() == 1);
    ASSERT(r2.is_ok() && r2.unwrap() == 2);
    ASSERT(r3.is_ok() && r3.unwrap() == 3);
})

TEST(task_void_success, {
    auto make_task = []() -> task<void> {
        co_return;
    };
    auto t = make_task();
    t.sync_wait();
    ASSERT(t.is_done());
})

TEST(task_void_return_value, {
    auto make_task = []() -> task<void> {
        co_return;
    };
    auto t = make_task();
    t.start();
    ASSERT(t.is_done());
})
