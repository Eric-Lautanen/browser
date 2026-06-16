#pragma once
#include <vector>
#include <string>
#include "../tests/utility.hpp"

namespace browser::net {

std::vector<u8> inflate(const u8* data, u32 len);
std::vector<u8> gzip_decompress(const u8* data, u32 len);

}
