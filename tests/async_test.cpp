#include "../async/executor.hpp"
#include "../async/task.hpp"
#include "test_framework.hpp"
#include "utility.hpp"

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

// BR-N2: abandon() releases ownership without destroying a suspended frame.
// PageLoader teardown uses this so an IOCP completion cannot resume into a
// destroyed coroutine. Before abandon(), destroying the task here would free
// the frame while it is still suspended at a yield point.
TEST(task_abandon_suspends_without_destroy, {
    auto make_task = []() -> task<int> {
        co_await task_yield{};
        co_return 5;
    };
    auto t = make_task();
    t.start();
    ASSERT(!t.is_done());  // suspended inside task_yield
    t.abandon();           // leak-on-purpose; must not touch the frame
})

// BR-N1: supersede-then-finish ordering — a superseded generation observes
// staleness and drains, exactly the checkpoint contract PageLoader relies on.
TEST(task_supersede_generation_pattern, {
    u64 generation = 0;
    int published_by = -1;

    auto load = [&](u64 gen) -> task<void> {
        co_await task_yield{};
        if (generation != gen) {
            // stale: publish nothing; the successor drains the slot
            co_return;
        }
        published_by = static_cast<int>(gen);
        co_return;
    };

    // first navigation owns the slot
    u64 g1 = ++generation;
    auto t1 = load(g1);
    t1.start();

    // second navigation supersedes before t1 resumes
    u64 g2 = ++generation;
    ASSERT(generation != g1);

    auto t2 = load(g2);
    t2.start();   // suspends at the yield like any pooled coroutine
    t2.resume();  // pool resumes it: generation matches -> publishes

    t1.start();  // now the stale task resumes: publishes nothing
    ASSERT(published_by == static_cast<int>(g2));
})
