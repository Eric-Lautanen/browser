#include "iocp.hpp"

namespace browser::net {

IOCP::IOCP() = default;

IOCP::~IOCP() {
    stop_workers();
    close();
}

Result<void> IOCP::create(u32 max_concurrency, u32 worker_count) {
    if (handle_) {
        close();
    }
    handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, max_concurrency);
    if (!handle_) {
        return std::string("CreateIoCompletionPort failed");
    }
    if (worker_count > 0) {
        auto r = start_workers(worker_count);
        if (r.is_err()) {
            close();
            return r;
        }
    }
    return {};
}

Result<void> IOCP::associate_handle(HANDLE handle, ULONG_PTR completion_key) {
    if (!is_valid()) {
        return std::string("IOCP not created");
    }
    HANDLE result = CreateIoCompletionPort(handle, handle_, completion_key, 0);
    if (!result) {
        return std::string("CreateIoCompletionPort associate failed");
    }
    return {};
}

Result<void> IOCP::associate_socket(SOCKET socket, ULONG_PTR completion_key) {
    return associate_handle(reinterpret_cast<HANDLE>(socket), completion_key);
}

BOOL IOCP::get_status(ULONG_PTR* completion_key, DWORD* bytes_transferred,
                      OVERLAPPED** overlapped, DWORD timeout) {
    return GetQueuedCompletionStatus(handle_, bytes_transferred,
                                     completion_key, overlapped, timeout);
}

BOOL IOCP::post_status(ULONG_PTR completion_key, DWORD bytes_transferred,
                       OVERLAPPED* overlapped) {
    return PostQueuedCompletionStatus(handle_, bytes_transferred,
                                      completion_key, overlapped);
}

Result<void> IOCP::start_workers(u32 count) {
    if (workers_running_) return {};
    if (!is_valid()) return std::string("IOCP not created");
    workers_running_ = true;
    if (count == 0) {
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        count = sysinfo.dwNumberOfProcessors * 2;
    }
    for (u32 i = 0; i < count; i++) {
        workers_.emplace_back([this]() { worker_thread(); });
    }
    return {};
}

void IOCP::stop_workers() {
    if (!workers_running_) return;
    workers_running_ = false;
    // Post a special shutdown packet for each worker
    for (std::size_t i = 0; i < workers_.size(); i++) {
        PostQueuedCompletionStatus(handle_, 0, 0, nullptr);
    }
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

void IOCP::worker_thread() {
    while (workers_running_) {
        ULONG_PTR key = 0;
        DWORD bytes = 0;
        OVERLAPPED* pol = nullptr;
        BOOL ok = GetQueuedCompletionStatus(handle_, &bytes, &key, &pol, INFINITE);
        if (!workers_running_) break;
        if (pol == nullptr) continue;
        auto* iol = static_cast<IoOverlapped*>(pol);
        if (!ok) {
            iol->error = GetLastError();
        }
        iol->bytes = bytes;
        iol->completed = true;
        if (iol->hEvent) {
            auto coro = std::coroutine_handle<>::from_address(iol->hEvent);
            coro.resume();
        }
    }
}

void IOCP::close() {
    stop_workers();
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

IOCP& IOCP::global() {
    static IOCP instance;
    return instance;
}

} // namespace browser::net
