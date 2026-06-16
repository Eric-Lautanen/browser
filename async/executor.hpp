#pragma once
#include <coroutine>
#include <windows.h>
#include "../tests/utility.hpp"

namespace browser::async {

struct thread_pool_executor {
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto cb = [](PTP_CALLBACK_INSTANCE, PVOID ctx, PTP_WORK) noexcept {
            auto* handle = static_cast<std::coroutine_handle<>*>(ctx);
            handle->resume();
            delete handle;
        };
        auto* ctx = new std::coroutine_handle<>(h);
        auto* work = CreateThreadpoolWork(cb, ctx, nullptr);
        if (work) {
            SubmitThreadpoolWork(work);
        } else {
            delete ctx;
        }
    }
    void await_resume() noexcept {}
};

struct io_executor {
    HANDLE iocp_;
    OVERLAPPED* overlapped_;

    io_executor(HANDLE iocp, OVERLAPPED* ol) : iocp_(iocp), overlapped_(ol) {}

    bool await_ready() noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
        overlapped_->hEvent = reinterpret_cast<HANDLE>(h.address());
        PostQueuedCompletionStatus(iocp_, 0, 0, overlapped_);
        return true;
    }

    void await_resume() noexcept {}
};

// Awaiter for IOCP-based socket operations.
// Stores the coroutine handle in the OVERLAPPED's hEvent field (unused by IOCP).
// The IOCP worker thread will extract it and resume the coroutine.
struct iocp_awaiter {
    OVERLAPPED* ol;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        ol->hEvent = reinterpret_cast<HANDLE>(h.address());
    }

    void await_resume() noexcept {}
};

// A simple awaitable that suspends unconditionally.
// Used when the coroutine needs to yield control back to the executor.
struct task_yield {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { (void)h; }
    void await_resume() noexcept {}
};

} // namespace browser::async
