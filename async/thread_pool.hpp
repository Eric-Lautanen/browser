#pragma once
#include <functional>
#include <vector>
#include <windows.h>
#include "../tests/utility.hpp"

namespace browser::async {

class thread_pool {
public:
    explicit thread_pool(u32 thread_count = 0) {
        if (thread_count == 0) {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            thread_count = si.dwNumberOfProcessors + 2;
        }
        pool_ = CreateThreadpool(nullptr);
        if (!pool_) return;
        SetThreadpoolThreadMinimum(pool_, 1);
        SetThreadpoolThreadMaximum(pool_, thread_count);
        cleanup_group_ = CreateThreadpoolCleanupGroup();
        if (!cleanup_group_) {
            CloseThreadpool(pool_);
            pool_ = nullptr;
        }
    }

    ~thread_pool() {
        if (pool_ && cleanup_group_) {
            CloseThreadpoolCleanupGroupMembers(cleanup_group_, FALSE, nullptr);
            CloseThreadpoolCleanupGroup(cleanup_group_);
        }
        if (pool_) {
            CloseThreadpool(pool_);
        }
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    bool is_valid() const { return pool_ != nullptr; }

    void enqueue(std::function<void()> fn) {
        if (!pool_) return;
        struct work_ctx {
            std::function<void()> func;
        };
        auto* ctx = new work_ctx{std::move(fn)};
        auto cb = [](PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) {
            auto* wctx = static_cast<work_ctx*>(context);
            wctx->func();
            delete wctx;
        };
        PTP_WORK work = CreateThreadpoolWork(cb, ctx, nullptr);
        if (work) {
            SubmitThreadpoolWork(work);
            CloseThreadpoolWork(work);
        } else {
            delete ctx;
        }
    }

private:
    PTP_POOL pool_ = nullptr;
    PTP_CLEANUP_GROUP cleanup_group_ = nullptr;
};

} // namespace browser::async
