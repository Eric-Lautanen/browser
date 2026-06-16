#include "decoder.hpp"

namespace browser::image {

std::unique_ptr<Decoder> create_bmp_decoder();
std::unique_ptr<Decoder> create_png_decoder();
std::unique_ptr<Decoder> create_gif_decoder();
std::unique_ptr<Decoder> create_jpeg_decoder();

std::unique_ptr<Decoder> create_decoder(ImageFormat format) {
    switch (format) {
        case ImageFormat::BMP:   return create_bmp_decoder();
        case ImageFormat::PNG:   return create_png_decoder();
        case ImageFormat::GIF:   return create_gif_decoder();
        case ImageFormat::JPEG:  return create_jpeg_decoder();
        default:                 return nullptr;
    }
}

} // namespace browser::image
