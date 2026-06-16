#pragma once
#include <vector>
#include <string>
#include "../tests/utility.hpp"
#include "../async/task.hpp"

namespace browser::net {

std::vector<u8> inflate(const u8* data, u32 len);
std::vector<u8> gzip_decompress(const u8* data, u32 len);

// Async versions that run on thread pool
async::task<std::vector<u8>> inflate_async(const u8* data, u32 len);
async::task<std::vector<u8>> gzip_decompress_async(const u8* data, u32 len);

}
