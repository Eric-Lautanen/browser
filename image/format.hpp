#pragma once
#include "../tests/utility.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace browser::image {

    enum class ImageFormat { PNG, JPEG, GIF, BMP, WEBP, TIFF, ICO, UNKNOWN };

    struct Image {
        u32 width = 0;
        u32 height = 0;
        ImageFormat format = ImageFormat::UNKNOWN;
        std::vector<u8> rgba_pixels;
    };

    inline ImageFormat detect_format(const u8 *data, size_t size) {
        if (size < 8)
            return ImageFormat::UNKNOWN;
        if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' && data[4] == 0x0D &&
            data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A)
            return ImageFormat::PNG;
        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
            return ImageFormat::JPEG;
        if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8' &&
            (data[4] == '7' || data[4] == '9') && data[5] == 'a')
            return ImageFormat::GIF;
        if (data[0] == 'B' && data[1] == 'M')
            return ImageFormat::BMP;
        if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' &&
            data[9] == 'E' && data[10] == 'B' && data[11] == 'P')
            return ImageFormat::WEBP;
        if (size >= 4 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 0x2A) ||
                          (data[0] == 'M' && data[1] == 'M' && data[2] == 0x2A)))
            return ImageFormat::TIFF;
        if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00)
            return ImageFormat::ICO;
        return ImageFormat::UNKNOWN;
    }

}  // namespace browser::image
