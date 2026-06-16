#pragma once
#include <windows.h>

namespace browser::async {

class mutex {
public:
    mutex() noexcept { InitializeSRWLock(&lock_); }
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

    void lock() noexcept { AcquireSRWLockExclusive(&lock_); }
    void unlock() noexcept { ReleaseSRWLockExclusive(&lock_); }
    bool try_lock() noexcept { return TryAcquireSRWLockExclusive(&lock_) != 0; }

private:
    SRWLOCK lock_;
};

} // namespace browser::async
