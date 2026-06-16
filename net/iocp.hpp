#pragma once
#include <windows.h>
#include <vector>
#include <thread>
#include <atomic>
#include <coroutine>
#include "../tests/utility.hpp"

namespace browser::net {

// Overlapped extension for IOCP dispatch.
// hEvent stores the coroutine handle (IOCP ignores hEvent).
struct IoOverlapped : OVERLAPPED {
    DWORD error = 0;
    DWORD bytes = 0;
    bool completed = false;

    IoOverlapped() {
        std::memset(static_cast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
        hEvent = nullptr;
    }

    std::coroutine_handle<> get_coro() const {
        return std::coroutine_handle<>::from_address(hEvent);
    }

    void set_coro(std::coroutine_handle<> h) {
        hEvent = reinterpret_cast<HANDLE>(h.address());
    }
};

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
