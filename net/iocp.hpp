#pragma once
// winsock2.h must precede windows.h; keep them out of IncludeOrder sorting.
// clang-format off
#include <winsock2.h>
#include <windows.h>
// clang-format on

#include "../tests/utility.hpp"

#include <atomic>
#include <coroutine>
#include <cstring>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace browser::net {

    // Overlapped extension for IOCP dispatch.
    // The resumable coroutine lives in `coro` (NOT in OVERLAPPED::hEvent — the
    // kernel validates/signals hEvent as an event object when non-null at I/O
    // initiation time). It is registered BEFORE the operation is issued so a
    // worker can never dequeue the completion packet before the handle is
    // visible (lost-wakeup race, audit N-C4).
    struct IoOverlapped : OVERLAPPED {
        DWORD error = 0;
        DWORD bytes = 0;
        bool completed = false;
        std::atomic<void *> coro{nullptr};

        IoOverlapped() {
            std::memset(static_cast<OVERLAPPED *>(this), 0, sizeof(OVERLAPPED));
            hEvent = nullptr;
        }

        std::coroutine_handle<> get_coro() const {
            return std::coroutine_handle<>::from_address(coro.load(std::memory_order_acquire));
        }

        void set_coro(std::coroutine_handle<> h) { coro.store(h.address(), std::memory_order_release); }
    };

    // Awaiter for overlapped I/O that closes the lost-wakeup race (N-C4): the
    // coroutine handle is registered into the IoOverlapped *before* the initiation
    // callable issues the operation, so an IOCP worker can never observe the
    // completion packet while no resume target is stored.
    //
    // Init must return 0 when the operation was initiated (a completion packet
    // will arrive; immediate success also queues one for IOCP-bound handles)
    // or non-zero on synchronous failure (no packet will ever arrive; the
    // coroutine resumes inline instead of suspending forever).
    template <typename Init>
    struct iocp_io_awaiter {
        IoOverlapped *ol;
        Init init;

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> h) {
            ol->set_coro(h);
            if (init() == 0)
                return true;
            ol->set_coro(nullptr);
            return false;
        }

        void await_resume() noexcept {}
    };

    template <typename Init>
    iocp_io_awaiter<std::decay_t<Init>> co_iocp(IoOverlapped *ol, Init &&init) {
        return {ol, std::forward<Init>(init)};
    }

class IOCP {
public:
    IOCP();
    ~IOCP();

    IOCP(const IOCP&) = delete;
    IOCP& operator=(const IOCP&) = delete;

    Result<void> create(u32 max_concurrency = 0, u32 worker_count = 0);
    Result<void> associate_handle(HANDLE handle, ULONG_PTR completion_key = 0);
    Result<void> associate_socket(SOCKET socket, ULONG_PTR completion_key = 0);

    BOOL get_status(ULONG_PTR* completion_key, DWORD* bytes_transferred,
                    OVERLAPPED** overlapped, DWORD timeout = INFINITE);

    BOOL post_status(ULONG_PTR completion_key, DWORD bytes_transferred,
                     OVERLAPPED* overlapped);

    HANDLE handle() const { return handle_; }
    bool is_valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    // Start/stop worker threads that dispatch completions to coroutines
    Result<void> start_workers(u32 count = 0);
    void stop_workers();
    bool workers_running() const { return workers_running_; }

    void close();

    // Global singleton IOCP instance
    static IOCP& global();

private:
    HANDLE handle_ = nullptr;
    std::vector<std::thread> workers_;
    std::atomic<bool> workers_running_{false};

    void worker_thread();
};

} // namespace browser::net
