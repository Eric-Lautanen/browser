#include "decoder.hpp"

namespace browser::image {

// JPEG decoder - Layer 2d deferred.
// WIC covers JPEG fully. A hand-written JPEG decoder (IDCT, Huffman, chroma upsampling)
// would be ~2000 lines. For now, WIC is always used for JPEG.
// This file exists as a placeholder so the build system knows about it.

std::unique_ptr<Decoder> create_jpeg_decoder() {
    // Return nullptr - WIC will be used as fallback
    return nullptr;
}

} // namespace browser::image
