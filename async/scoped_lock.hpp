#pragma once
#include "mutex.hpp"

namespace browser::async {

template<typename M = mutex>
class scoped_lock {
public:
    explicit scoped_lock(M& mtx) : mtx_(&mtx) { mtx_->lock(); }
    ~scoped_lock() { if (mtx_) mtx_->unlock(); }
    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;

private:
    M* mtx_;
};

} // namespace browser::async
