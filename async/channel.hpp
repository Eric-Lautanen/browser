#pragma once
#include <atomic>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "../tests/utility.hpp"

namespace browser::async {

template<typename T>
class channel {
public:
    explicit channel(size_t capacity = 0) : capacity_(capacity) {}

    channel(const channel&) = delete;
    channel& operator=(const channel&) = delete;

    bool try_send(T value) {
        if (capacity_ > 0) {
            std::lock_guard<std::mutex> lock(mtx_);
            if (queue_.size() >= capacity_) return false;
            queue_.push(std::move(value));
            count_.store(queue_.size(), std::memory_order_release);
        } else {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    void send(T value) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (capacity_ > 0) {
            cv_.wait(lock, [this] { return queue_.size() < capacity_; });
        }
        queue_.push(std::move(value));
        if (capacity_ > 0) {
            count_.store(queue_.size(), std::memory_order_release);
        }
        lock.unlock();
        cv_.notify_one();
    }

    std::optional<T> try_receive() {
        std::unique_lock<std::mutex> lock(mtx_);
        if (queue_.empty()) return std::nullopt;
        T val = std::move(queue_.front());
        queue_.pop();
        if (capacity_ > 0) {
            count_.store(queue_.size(), std::memory_order_release);
        }
        lock.unlock();
        cv_.notify_one();
        return val;
    }

    T receive() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
        T val = std::move(queue_.front());
        queue_.pop();
        if (capacity_ > 0) {
            count_.store(queue_.size(), std::memory_order_release);
        }
        lock.unlock();
        cv_.notify_one();
        return val;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

private:
    size_t capacity_;
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<size_t> count_{0};
    bool closed_ = false;
};

} // namespace browser::async
