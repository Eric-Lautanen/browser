#include "iocp.hpp"

namespace browser::net {

IOCP::IOCP() = default;

IOCP::~IOCP() {
    close();
}

Result<void> IOCP::create(u32 max_concurrency) {
    if (handle_) {
        close();
    }
    handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, max_concurrency);
    if (!handle_) {
        return std::string("CreateIoCompletionPort failed");
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

void IOCP::close() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

} // namespace browser::net
