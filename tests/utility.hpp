#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace browser {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;

// Result type for fallible operations (non-void T)
template<typename T, typename E = std::string>
class Result {
    static_assert(!std::is_same_v<T, void>, "Use Result<void, E> for void type");
    static_assert(!std::is_same_v<T, E>, "Result<T, E> requires T != E to avoid constructor ambiguity");
    static constexpr u32 kStorageSize = sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E);
    alignas(T) alignas(E) std::byte storage_[kStorageSize];
    bool ok_;
    T* value_ptr() { return reinterpret_cast<T*>(storage_); }
    const T* value_ptr() const { return reinterpret_cast<const T*>(storage_); }
    E* error_ptr() { return reinterpret_cast<E*>(storage_); }
    const E* error_ptr() const { return reinterpret_cast<const E*>(storage_); }
    void destroy() { if (ok_) value_ptr()->~T(); else error_ptr()->~E(); }
public:
    // Value constructor (disabled when argument type matches E to avoid ambiguity)
    // Note: Result<T, E> with T == E is not supported; use a wrapper type if needed.
    template<typename U, std::enable_if_t<std::is_constructible_v<T, U&&> && !std::is_same_v<std::decay_t<U>, E>, int> = 0>
    Result(U&& v) : ok_(true) { ::new (storage_) T(std::forward<U>(v)); }
    Result(const E& e) : ok_(false) { ::new (storage_) E(e); }
    Result(E&& e) : ok_(false) { ::new (storage_) E(std::move(e)); }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result(Result&& other) noexcept : ok_(other.ok_) {
        if (ok_) ::new (storage_) T(std::move(*other.value_ptr()));
        else ::new (storage_) E(std::move(*other.error_ptr()));
        // Moved-from object retains ok_ so ~Result() destroys the moved-from value safely
    }
    Result& operator=(Result&& other) noexcept {
        if (this != &other) { destroy(); ok_ = other.ok_;
            if (ok_) ::new (storage_) T(std::move(*other.value_ptr()));
            else ::new (storage_) E(std::move(*other.error_ptr()));
        } return *this;
    }
    ~Result() { destroy(); }
    bool is_ok() const { return ok_; }
    bool is_err() const { return !ok_; }
    T& unwrap() { return *value_ptr(); }
    const T& unwrap() const { return *value_ptr(); }
    E& unwrap_err() { return *error_ptr(); }
    const E& unwrap_err() const { return *error_ptr(); }
};

// Result<void, E> specialization
template<typename E>
class Result<void, E> {
    alignas(E) std::byte storage_[sizeof(E)];
    bool ok_;
    E* error_ptr() { return reinterpret_cast<E*>(storage_); }
    const E* error_ptr() const { return reinterpret_cast<const E*>(storage_); }
public:
    Result() : ok_(true) {}
    Result(const E& e) : ok_(false) { ::new (storage_) E(e); }
    Result(E&& e) : ok_(false) { ::new (storage_) E(std::move(e)); }
    Result(const Result&) = delete;
    Result(Result&& other) noexcept : ok_(other.ok_) {
        if (!ok_) {
            ::new (storage_) E(std::move(*other.error_ptr()));
        }
        other.ok_ = true; // source: error was moved out, treat as value
    }
    ~Result() { if (!ok_) error_ptr()->~E(); }
    bool is_ok() const { return ok_; }
    bool is_err() const { return !ok_; }
    E& unwrap_err() { return *error_ptr(); }
    const E& unwrap_err() const { return *error_ptr(); }
};

} // namespace browser
