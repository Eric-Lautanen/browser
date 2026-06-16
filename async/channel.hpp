#pragma once
#include <atomic>
#include <optional>
#include <new>
#include <type_traits>
#include <cstddef>
#include "../tests/utility.hpp"

namespace browser::async {

template<typename T>
class channel {
public:
    explicit channel(size_t capacity = 0) {
        if (capacity == 0) capacity = 4096;
        size_t p2 = 1;
        while (p2 < capacity + 2) p2 <<= 1;
        capacity_ = p2;
        mask_ = capacity_ - 1;
        usable_capacity_ = capacity;
        buffer_ = static_cast<T*>(::operator new(capacity_ * sizeof(T)));
    }

    ~channel() {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_relaxed);
        while (h != t) {
            buffer_[h].~T();
            h = (h + 1) & mask_;
        }
        ::operator delete(buffer_);
    }

    channel(const channel&) = delete;
    channel& operator=(const channel&) = delete;

    bool try_send(T value) {
        size_t t = tail_.load(std::memory_order_relaxed);
        size_t h = head_.load(std::memory_order_acquire);
        size_t next = (t + 1) & mask_;
        if (next == h || ((t - h) & mask_) >= usable_capacity_) return false;
        ::new (&buffer_[t]) T(std::move(value));
        tail_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> try_receive() {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_acquire);
        if (h == t) return std::nullopt;
        T val(std::move(buffer_[h]));
        buffer_[h].~T();
        head_.store((h + 1) & mask_, std::memory_order_release);
        return val;
    }

    void send(T value) {
        for (;;) {
            size_t t = tail_.load(std::memory_order_relaxed);
            size_t h = head_.load(std::memory_order_acquire);
            size_t next = (t + 1) & mask_;
            if (next != h) {
                ::new (&buffer_[t]) T(std::move(value));
                tail_.store(next, std::memory_order_release);
                return;
            }
            __builtin_ia32_pause();
        }
    }

    T receive() {
        for (;;) {
            size_t h = head_.load(std::memory_order_relaxed);
            size_t t = tail_.load(std::memory_order_acquire);
            if (h != t) {
                T val(std::move(buffer_[h]));
                buffer_[h].~T();
                head_.store((h + 1) & mask_, std::memory_order_release);
                return val;
            }
            __builtin_ia32_pause();
        }
    }

    void close() {
        closed_.store(true, std::memory_order_release);
    }

    bool is_closed() const {
        return closed_.load(std::memory_order_acquire);
    }

    size_t size() const {
        size_t t = tail_.load(std::memory_order_acquire);
        size_t h = head_.load(std::memory_order_acquire);
        return (t - h) & mask_;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    size_t capacity() const { return usable_capacity_; }

private:
    size_t capacity_;
    size_t mask_;
    size_t usable_capacity_;
    T* buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    std::atomic<bool> closed_{false};
};

} // namespace browser::async
