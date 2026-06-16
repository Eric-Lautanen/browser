#pragma once
#include <windows.h>
#include <vector>
#include "../tests/utility.hpp"

namespace browser::net {

class IOCP {
public:
    IOCP();
    ~IOCP();

    IOCP(const IOCP&) = delete;
    IOCP& operator=(const IOCP&) = delete;

    Result<void> create(u32 max_concurrency = 0);
    Result<void> associate_handle(HANDLE handle, ULONG_PTR completion_key = 0);
    Result<void> associate_socket(SOCKET socket, ULONG_PTR completion_key = 0);

    BOOL get_status(ULONG_PTR* completion_key, DWORD* bytes_transferred,
                    OVERLAPPED** overlapped, DWORD timeout = INFINITE);

    BOOL post_status(ULONG_PTR completion_key, DWORD bytes_transferred,
                     OVERLAPPED* overlapped);

    HANDLE handle() const { return handle_; }
    bool is_valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

    void close();

private:
    HANDLE handle_ = nullptr;
};

} // namespace browser::net
