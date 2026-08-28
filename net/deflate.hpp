#pragma once
#include "../async/task.hpp"
#include "../core/utility.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace browser::net {

    // Hard cap on decompressed size (N-S8: decompression-bomb defense).
    inline constexpr std::size_t kMaxInflateOutput = 512ull * 1024 * 1024;

    // Inflates a deflate stream. Auto-detects and validates a zlib wrapper
    // (CMF/FLG header + Adler-32 trailer); plain deflate data is accepted as-is.
    // Returns an empty vector on malformed input, failed Adler-32 check, or when
    // the output cap is exceeded.
    std::vector<u8> inflate(const u8 *data, u32 len, std::size_t max_output = kMaxInflateOutput);

    // Raw-deflate entry point (no zlib wrapper, no Adler-32).
    std::vector<u8> inflate_raw(const u8 *data, u32 len, std::size_t max_output = kMaxInflateOutput);

    std::vector<u8> gzip_decompress(const u8 *data, u32 len, std::size_t max_output = kMaxInflateOutput);

    // Async versions that run on thread pool
    async::task<std::vector<u8>> inflate_async(const u8 *data, u32 len);
    async::task<std::vector<u8>> gzip_decompress_async(const u8 *data, u32 len);

}
