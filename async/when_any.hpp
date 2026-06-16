#pragma once
#include <atomic>
#include <coroutine>
#include <optional>
#include <variant>
#include <utility>
#include "../tests/utility.hpp"
#include "task.hpp"

namespace browser::async {

namespace detail {

template<typename... Tasks>
struct when_any_awaiter {
    std::tuple<Tasks...> tasks_;
    std::atomic<bool> done_{false};
    std::coroutine_handle<> continuation_ = nullptr;
    size_t index_ = 0;

    when_any_awaiter(Tasks&&... t) : tasks_(std::move(t)...) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        continuation_ = h;
        start_all(std::index_sequence_for<Tasks...>{});
    }

    size_t await_resume() { return index_; }

private:
    template<size_t... I>
    void start_all(std::index_sequence<I...>) {
        (start_one<I>(), ...);
    }

    template<size_t I>
    void start_one() {
        auto& t = std::get<I>(tasks_);
        struct awaiter {
            task<std::decay_t<decltype(std::get<I>(std::declval<std::tuple<Tasks...>&>()))>>* t_;
            std::coroutine_handle<> parent_;
            std::atomic<bool>* done_;
            size_t index_;
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
                t_->resume();
                return h;
            }
            void await_resume() noexcept {}
        };
        static_cast<void>(t);
    }
};

} // namespace detail

template<typename... Tasks>
auto when_any(Tasks&&... tasks) {
    return detail::when_any_awaiter<std::decay_t<Tasks>...>(std::forward<Tasks>(tasks)...);
}

} // namespace browser::async
