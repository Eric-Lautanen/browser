#include "decoder.hpp"

namespace browser::image {

// Forward declarations of format-specific creator functions
std::unique_ptr<Decoder> create_wic_decoder();
std::unique_ptr<Decoder> create_bmp_decoder();
std::unique_ptr<Decoder> create_png_decoder();
std::unique_ptr<Decoder> create_gif_decoder();
std::unique_ptr<Decoder> create_jpeg_decoder();

std::unique_ptr<Decoder> create_decoder(ImageFormat format) {
    // Hand-written decoders take priority
    switch (format) {
        case ImageFormat::BMP:
            return create_bmp_decoder();
        case ImageFormat::PNG:
            return create_png_decoder();
        case ImageFormat::GIF:
            return create_gif_decoder();
        case ImageFormat::JPEG: {
            auto jpeg = create_jpeg_decoder();
            if (jpeg) return jpeg;
            break;
        }
        default:
            break;
    }
    // Fall back to WIC for everything else
    return create_wic_decoder();
}

} // namespace browser::image
