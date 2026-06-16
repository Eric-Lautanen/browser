#pragma once
#include <tuple>
#include <atomic>
#include <coroutine>
#include <optional>
#include <utility>
#include "../tests/utility.hpp"
#include "task.hpp"

namespace browser::async {

namespace detail {

template<typename... Tasks>
struct when_all_awaiter {
    std::tuple<Tasks...> tasks_;
    std::atomic<size_t> count_{sizeof...(Tasks)};
    std::coroutine_handle<> continuation_ = nullptr;

    when_all_awaiter(Tasks&&... t) : tasks_(std::move(t)...) {}

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        continuation_ = h;
        start_all(std::index_sequence_for<Tasks...>{});
    }

    auto await_resume() {
        return collect_results(std::index_sequence_for<Tasks...>{});
    }

private:
    template<size_t... I>
    void start_all(std::index_sequence<I...>) {
        auto shared = std::make_shared<std::atomic<size_t>>(sizeof...(Tasks));
        (start_one<I>(*shared), ...);
    }

    template<size_t I>
    void start_one(std::atomic<size_t>& shared) {
        auto& t = std::get<I>(tasks_);
        struct shared_awaiter {
            task<std::tuple_element_t<I, std::tuple<Tasks...>>>* t_;
            std::coroutine_handle<> parent_;
            std::atomic<size_t>* shared_;
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
                t_->resume();
                return h;
            }
            void await_resume() noexcept {}
        };
        static_cast<void>(shared);
    }

    template<size_t... I>
    auto collect_results(std::index_sequence<I...>) {
        return std::make_tuple(std::move(std::get<I>(tasks_).await_resume())...);
    }
};

} // namespace detail

template<typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return detail::when_all_awaiter<std::decay_t<Tasks>...>(std::forward<Tasks>(tasks)...);
}

} // namespace browser::async
