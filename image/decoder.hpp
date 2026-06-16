#pragma once
#include <memory>
#include "format.hpp"

namespace browser::image {

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual Result<Image> decode(const u8* data, size_t size) = 0;
};

std::unique_ptr<Decoder> create_decoder(ImageFormat format);

} // namespace browser::image
