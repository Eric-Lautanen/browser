#pragma once
// Non-throwing numeric parsing (X-C1 / F-2).
//
// Under -fno-exceptions every std::sto* call site compiles its
// invalid_argument/out_of_range throw into a process abort, so any malformed
// URL host, CSS value, Cache-Control header or settings query could kill the
// browser. These helpers parse the FULL string and return std::nullopt on any
// failure instead of throwing.

#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

namespace browser::parse {

    inline std::optional<uint64_t> parse_u64(std::string_view s, int base = 10) {
        // Trim surrounding ASCII whitespace (headers carry plenty).
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        if (s.empty())
            return std::nullopt;
        uint64_t v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v, base);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size())
            return std::nullopt;
        return v;
    }

    inline std::optional<int64_t> parse_i64(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        if (s.empty())
            return std::nullopt;
        int64_t v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size())
            return std::nullopt;
        return v;
    }

    inline std::optional<float> parse_f32(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        if (s.empty())
            return std::nullopt;
        // Reject NaN/Inf spellings and hex floats; decimal only.
        for (char c : s) {
            if (c == 'x' || c == 'X' || c == 'n' || c == 'N' || c == 'i' || c == 'I')
                return std::nullopt;
        }
        float v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (res.ec != std::errc{} || res.ptr != s.data() + s.size())
            return std::nullopt;
        return v;
    }

}  // namespace browser::parse
