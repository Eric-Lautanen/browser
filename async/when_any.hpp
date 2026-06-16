#pragma once
#include <atomic>
#include <coroutine>
#include <utility>
#include "../tests/utility.hpp"
#include "task.hpp"

namespace browser::async {

namespace detail {

struct when_any_context {
    std::atomic<bool> done_{false};
    std::coroutine_handle<> continuation_ = nullptr;
    size_t index_ = 0;

    bool try_complete(size_t idx) {
        bool expected = false;
        if (done_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            index_ = idx;
            if (continuation_) continuation_.resume();
            return true;
        }
        return false;
    }
};

template<typename T>
task<void> make_when_any_task(task<T> t, when_any_context* ctx, size_t idx) {
    co_await t;
    ctx->try_complete(idx);
}

template<>
inline task<void> make_when_any_task<void>(task<void> t, when_any_context* ctx, size_t idx) {
    co_await t;
    ctx->try_complete(idx);
}

template<typename... Tasks>
struct when_any_awaiter {
    std::tuple<std::decay_t<Tasks>...> tasks_;
    when_any_context ctx_;

    when_any_awaiter(Tasks&&... t) : tasks_(std::move(t)...) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        ctx_.continuation_ = h;
        start_tasks(std::index_sequence_for<Tasks...>{});
    }

    size_t await_resume() {
        return ctx_.index_;
    }

private:
    template<size_t... I>
    void start_tasks(std::index_sequence<I...>) {
        (launch<I>(), ...);
    }

    template<size_t I>
    void launch() {
        auto& t = std::get<I>(tasks_);
        make_when_any_task(std::move(t), &ctx_, I).start();
    }
};

} // namespace detail

template<typename... Tasks>
auto when_any(Tasks&&... tasks) {
    return detail::when_any_awaiter<std::decay_t<Tasks>...>(std::forward<Tasks>(tasks)...);
}

} // namespace browser::async
