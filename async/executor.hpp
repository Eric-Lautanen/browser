#pragma once
#include <coroutine>
#include <windows.h>
#include "../tests/utility.hpp"

namespace browser::async {

struct thread_pool_executor {
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        auto cb = [](PTP_CALLBACK_INSTANCE, PVOID ctx, PTP_WORK) {
            auto* handle = static_cast<std::coroutine_handle<>*>(ctx);
            handle->resume();
            delete handle;
        };
        auto* ctx = new std::coroutine_handle<>(h);
        auto* work = CreateThreadpoolWork(cb, ctx, nullptr);
        if (work) {
            SubmitThreadpoolWork(work);
            CloseThreadpoolWork(work);
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
        overlapped_->u.HighOffset = 0;
        overlapped_->u.LowOffset = 0;
        overlapped_->u.s_inner = reinterpret_cast<ULONG_PTR>(h.address());
        PostQueuedCompletionStatus(iocp_, 0, 0, overlapped_);
        return true;
    }

    void await_resume() noexcept {}
};

} // namespace browser::async
