#pragma once
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>
#include <atomic>
#include <cstdlib>
#include "../tests/utility.hpp"

namespace browser::async {

template<typename T>
class task;

template<typename T>
class task_promise {
public:
    task<T> get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<task_promise> h) noexcept {
            auto& p = h.promise();
            if (p.continuation_) return p.continuation_;
            p.done_.store(true, std::memory_order_release);
            return std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept { std::abort(); }
    template<typename U>
    void return_value(U&& v) { result_.emplace(std::forward<U>(v)); }
    Result<T>& result() { return *result_; }
    bool has_result() const { return result_.has_value(); }
    bool is_done() const { return done_.load(std::memory_order_acquire); }
    std::coroutine_handle<> continuation_ = nullptr;
private:
    std::optional<Result<T>> result_;
    std::atomic<bool> done_{false};
};

template<>
class task_promise<void> {
public:
    task<void> get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<task_promise> h) noexcept {
            auto& p = h.promise();
            if (p.continuation_) return p.continuation_;
            p.done_.store(true, std::memory_order_release);
            return std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept { std::abort(); }
    void return_void() { done_.store(true, std::memory_order_release); }
    bool is_done() const { return done_.load(std::memory_order_acquire); }
    std::coroutine_handle<> continuation_ = nullptr;
private:
    std::atomic<bool> done_{false};
};

template<typename T>
class task {
public:
    using promise_type = task_promise<T>;
    task() noexcept : coro_(nullptr) {}
    explicit task(std::coroutine_handle<promise_type> h) noexcept : coro_(h) {}
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& other) noexcept : coro_(std::exchange(other.coro_, nullptr)) {}
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = std::exchange(other.coro_, nullptr);
        }
        return *this;
    }
    ~task() { if (coro_) { coro_.destroy(); } }

    bool await_ready() noexcept { return coro_.promise().is_done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        coro_.promise().continuation_ = awaiting;
        return coro_;
    }
    Result<T> await_resume() { return std::move(coro_.promise().result()); }

    bool is_done() const { return !coro_ || coro_.done(); }
    void resume() { if (coro_ && !coro_.done()) coro_.resume(); }
    void start() { resume(); }
    Result<T> sync_wait() {
        start();
        while (!is_done()) {}
        return await_resume();
    }

private:
    std::coroutine_handle<promise_type> coro_;
    friend class task_promise<T>;
};

template<>
class task<void> {
public:
    using promise_type = task_promise<void>;
    task() noexcept : coro_(nullptr) {}
    explicit task(std::coroutine_handle<promise_type> h) noexcept : coro_(h) {}
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& other) noexcept : coro_(std::exchange(other.coro_, nullptr)) {}
    task& operator=(task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = std::exchange(other.coro_, nullptr);
        }
        return *this;
    }
    ~task() { if (coro_) coro_.destroy(); }

    bool await_ready() noexcept { return coro_.promise().is_done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        coro_.promise().continuation_ = awaiting;
        return coro_;
    }
    void await_resume() {}

    bool is_done() const { return !coro_ || coro_.done(); }
    void resume() { if (coro_ && !coro_.done()) coro_.resume(); }
    void start() { resume(); }
    void sync_wait() {
        start();
        while (!is_done()) {}
    }

private:
    std::coroutine_handle<promise_type> coro_;
    friend class task_promise<void>;
};

template<typename T>
inline task<T> task_promise<T>::get_return_object() noexcept {
    return task<T>{std::coroutine_handle<task_promise<T>>::from_promise(*this)};
}

inline task<void> task_promise<void>::get_return_object() noexcept {
    return task<void>{std::coroutine_handle<task_promise<void>>::from_promise(*this)};
}

} // namespace browser::async
