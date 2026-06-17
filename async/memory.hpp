#pragma once
#include "../tests/utility.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <windows.h>

namespace browser::async {

    class Mallocator {
    public:
        Mallocator() { heap_ = HeapCreate(0, 1024 * 1024, 0); }

        ~Mallocator() {
            if (heap_) {
                report_leaks();
                HeapDestroy(heap_);
            }
        }

        Mallocator(const Mallocator &) = delete;
        Mallocator &operator=(const Mallocator &) = delete;

        void *allocate(size_t size) {
            if (!heap_)
                return std::malloc(size);
            void *ptr = HeapAlloc(heap_, 0, size);
            if (ptr) {
                total_allocated_.fetch_add(size, std::memory_order_relaxed);
                alloc_count_.fetch_add(1, std::memory_order_relaxed);
            }
            return ptr;
        }

        void deallocate(void *ptr, size_t size = 0) {
            if (!heap_) {
                std::free(ptr);
                return;
            }
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
                std::fprintf(stderr,
                             "MEMORY LEAK: %zu allocations still alive (%zu bytes)\n",
                             count,
                             total_allocated_.load(std::memory_order_relaxed));
            } else {
                std::fprintf(stderr, "MEMORY OK: no leaks detected\n");
            }
        }

        static Mallocator &instance() {
            static Mallocator inst;
            return inst;
        }

    private:
        HANDLE heap_ = nullptr;
        std::atomic<size_t> total_allocated_{0};
        std::atomic<size_t> alloc_count_{0};
    };

    template <typename T>
    struct allocator {
        using value_type = T;

        allocator() = default;

        template <typename U>
        allocator(const allocator<U> &) {}

        T *allocate(size_t n) { return static_cast<T *>(Mallocator::instance().allocate(n * sizeof(T))); }

        void deallocate(T *ptr, size_t n) { Mallocator::instance().deallocate(ptr, n * sizeof(T)); }
    };

    template <typename T, typename U>
    bool operator==(const allocator<T> &, const allocator<U> &) {
        return true;
    }

    template <typename T, typename U>
    bool operator!=(const allocator<T> &, const allocator<U> &) {
        return false;
    }

    // Per-subsystem allocation tracking
    struct AllocationBreakdown {
        u64 html = 0;
        u64 css = 0;
        u64 js = 0;
        u64 net = 0;
        u64 render = 0;
        u64 image = 0;
        u64 platform = 0;
        u64 async = 0;
        u64 browser = 0;
        u64 unknown = 0;

        static AllocationBreakdown &instance() {
            static AllocationBreakdown inst;
            return inst;
        }

        void add(const char *subsystem, i64 bytes) {
            if (!subsystem) {
                unknown += (u64)(bytes > 0 ? bytes : 0);
                return;
            }
            std::string_view sv(subsystem);
            if (sv == "html")
                html += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "css")
                css += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "js")
                js += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "net")
                net += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "render")
                render += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "image")
                image += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "platform")
                platform += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "async")
                async += (u64)(bytes > 0 ? bytes : 0);
            else if (sv == "browser")
                browser += (u64)(bytes > 0 ? bytes : 0);
            else
                unknown += (u64)(bytes > 0 ? bytes : 0);
        }

        std::string to_json() const {
            char buf[512];
            snprintf(buf,
                     sizeof(buf),
                     "{\"html\":%llu,\"css\":%llu,\"js\":%llu,\"net\":%llu,"
                     "\"render\":%llu,\"image\":%llu,\"platform\":%llu,"
                     "\"async\":%llu,\"browser\":%llu,\"unknown\":%llu}",
                     (unsigned long long)html,
                     (unsigned long long)css,
                     (unsigned long long)js,
                     (unsigned long long)net,
                     (unsigned long long)render,
                     (unsigned long long)image,
                     (unsigned long long)platform,
                     (unsigned long long)async,
                     (unsigned long long)browser,
                     (unsigned long long)unknown);
            return buf;
        }

        void reset() { html = css = js = net = render = image = platform = async = browser = unknown = 0; }
    };

}  // namespace browser::async
