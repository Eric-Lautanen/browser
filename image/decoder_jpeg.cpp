#include "decoder.hpp"

namespace browser::image {

struct JPEGMarkers {
    bool soi_found = false;
    bool app0_found = false;
    bool dqt_found = false;
    bool sof_found = false;
    bool dht_found = false;
    bool sos_found = false;
    bool eoi_found = false;
};

static bool next_marker(const u8* data, size_t size, size_t& pos, u8& marker) {
    while (pos < size) {
        if (data[pos] == 0xFF) {
            pos++;
            while (pos < size && data[pos] == 0xFF) pos++;
            if (pos < size && data[pos] != 0x00) {
                marker = data[pos];
                pos++;
                return true;
            }
        } else {
            pos++;
        }
    }
    return false;
}

struct JPEGDecoder : Decoder {
    Result<Image> decode(const u8* data, size_t size) override {
        JPEGMarkers markers;
        size_t pos = 0;
        u8 marker = 0;

        while (next_marker(data, size, pos, marker)) {
            switch (marker) {
                case 0xD8: markers.soi_found = true; break;
                case 0xD9: markers.eoi_found = true; break;
                case 0xE0: markers.app0_found = true; break;
                case 0xDB: markers.dqt_found = true; break;
                case 0xC0: markers.sof_found = true; break;
                case 0xC4: markers.dht_found = true; break;
                case 0xDA: markers.sos_found = true; break;
                case 0xFE: break;
                default: break;
            }
        }

        if (!markers.soi_found || !markers.eoi_found) {
            return std::string("JPEG: missing SOI or EOI marker");
        }

        Image img;
        img.width = 0;
        img.height = 0;
        img.format = ImageFormat::JPEG;
        return img;
    }
};

std::unique_ptr<Decoder> create_jpeg_decoder() {
    return std::make_unique<JPEGDecoder>();
}

} // namespace browser::image