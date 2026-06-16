#pragma once
#include <cstdlib>
#include <cstdio>
#include <atomic>
#include <new>
#include <windows.h>
#include "../tests/utility.hpp"

namespace browser::async {

class Mallocator {
public:
    Mallocator() {
        heap_ = HeapCreate(0, 1024 * 1024, 0);
    }

    ~Mallocator() {
        if (heap_) {
            report_leaks();
            HeapDestroy(heap_);
        }
    }

    Mallocator(const Mallocator&) = delete;
    Mallocator& operator=(const Mallocator&) = delete;

    void* allocate(size_t size) {
        if (!heap_) return std::malloc(size);
        void* ptr = HeapAlloc(heap_, 0, size);
        if (ptr) {
            total_allocated_.fetch_add(size, std::memory_order_relaxed);
            alloc_count_.fetch_add(1, std::memory_order_relaxed);
        }
        return ptr;
    }

    void deallocate(void* ptr, size_t size = 0) {
        if (!heap_) { std::free(ptr); return; }
        if (ptr) {
            HeapFree(heap_, 0, ptr);
            total_allocated_.fetch_sub(size > 0 ? size : 0, std::memory_order_relaxed);
            alloc_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    size_t total_allocated() const { return total_allocated_.load(std::memory_order_relaxed); }
    size_t alloc_count() const { return alloc_count_.load(std::memory_order_relaxed); }

    void report_leaks() {
        size_t count = alloc_count_.load(std::memory_order_relaxed);
        if (count > 0) {
            std::fprintf(stderr, "MEMORY LEAK: %zu allocations still alive (%zu bytes)\n",
                         count, total_allocated_.load(std::memory_order_relaxed));
        } else {
            std::fprintf(stderr, "MEMORY OK: no leaks detected\n");
        }
    }

    static Mallocator& instance() {
        static Mallocator inst;
        return inst;
    }

private:
    HANDLE heap_ = nullptr;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> alloc_count_{0};
};

template<typename T>
struct allocator {
    using value_type = T;

    allocator() = default;

    template<typename U>
    allocator(const allocator<U>&) {}

    T* allocate(size_t n) {
        return static_cast<T*>(Mallocator::instance().allocate(n * sizeof(T)));
    }

    void deallocate(T* ptr, size_t n) {
        Mallocator::instance().deallocate(ptr, n * sizeof(T));
    }
};

template<typename T, typename U>
bool operator==(const allocator<T>&, const allocator<U>&) { return true; }

template<typename T, typename U>
bool operator!=(const allocator<T>&, const allocator<U>&) { return false; }

} // namespace browser::async
