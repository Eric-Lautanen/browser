#pragma once
#include <tuple>
#include <atomic>
#include <coroutine>
#include <memory>
#include <utility>
#include "../tests/utility.hpp"
#include "task.hpp"

namespace browser::async {

namespace detail {

struct when_all_context {
    std::atomic<size_t> remaining_;
    std::coroutine_handle<> continuation_ = nullptr;
    bool completed_ = false;

    explicit when_all_context(size_t count) : remaining_(count) {}

    bool task_completed() {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            completed_ = true;
            if (continuation_) continuation_.resume();
            return true;
        }
        return false;
    }
};

template<typename T>
task<void> make_when_all_task(task<T> t, when_all_context* ctx, Result<T>* out) {
    auto r = co_await t;
    if (out) *out = std::move(r);
    ctx->task_completed();
}

template<>
inline task<void> make_when_all_task<void>(task<void> t, when_all_context* ctx, void*) {
    co_await t;
    ctx->task_completed();
}

template<typename... Tasks>
struct when_all_awaiter {
    std::tuple<std::decay_t<Tasks>...> tasks_;
    when_all_context ctx_;
    using result_storage = std::tuple<Result<typename std::decay_t<Tasks>::result_type>...>;
    result_storage results_;

    when_all_awaiter(Tasks&&... t)
        : tasks_(std::move(t)...), ctx_(sizeof...(Tasks)) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        ctx_.continuation_ = h;
        start_tasks(std::index_sequence_for<Tasks...>{});
    }

    result_storage await_resume() {
        return std::move(results_);
    }

private:
    template<size_t... I>
    void start_tasks(std::index_sequence<I...>) {
        (launch<I>(), ...);
    }

    template<size_t I>
    void launch() {
        auto& t = std::get<I>(tasks_);
        make_when_all_task(std::move(t), &ctx_, &std::get<I>(results_)).start();
    }
};

} // namespace detail

template<typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return detail::when_all_awaiter<Tasks...>(std::forward<Tasks>(tasks)...);
}

} // namespace browser::async
