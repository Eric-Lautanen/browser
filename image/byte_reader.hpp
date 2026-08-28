#pragma once
#include "../core/utility.hpp"
#include <cstring>
#include <string>

namespace browser::image {

// I-refactor: Shared checked byte reader for all decoders. Eliminates ad-hoc
// bounds checks that led to I-C3/C4/C5/H3.
class ByteReader {
public:
    ByteReader(const u8* data, size_t size) : data_(data), size_(size), pos_(0) {}

    size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }
    size_t pos() const { return pos_; }
    bool eof() const { return pos_ >= size_; }

    Result<u8> u8() {
        if (pos_ + 1 > size_) return std::string("truncated u8");
        return data_[pos_++];
    }
    Result<u16> u16_le() {
        if (pos_ + 2 > size_) return std::string("truncated u16");
        u16 v = static_cast<u16>(data_[pos_]) | (static_cast<u16>(data_[pos_+1]) << 8);
        pos_ += 2;
        return v;
    }
    Result<u16> u16_be() {
        if (pos_ + 2 > size_) return std::string("truncated u16");
        u16 v = (static_cast<u16>(data_[pos_]) << 8) | static_cast<u16>(data_[pos_+1]);
        pos_ += 2;
        return v;
    }
    Result<u32> u32_le() {
        if (pos_ + 4 > size_) return std::string("truncated u32");
        u32 v = static_cast<u32>(data_[pos_]) |
                (static_cast<u32>(data_[pos_+1]) << 8) |
                (static_cast<u32>(data_[pos_+2]) << 16) |
                (static_cast<u32>(data_[pos_+3]) << 24);
        pos_ += 4;
        return v;
    }
    Result<u32> u32_be() {
        if (pos_ + 4 > size_) return std::string("truncated u32");
        u32 v = (static_cast<u32>(data_[pos_]) << 24) |
                (static_cast<u32>(data_[pos_+1]) << 16) |
                (static_cast<u32>(data_[pos_+2]) << 8) |
                static_cast<u32>(data_[pos_+3]);
        pos_ += 4;
        return v;
    }
    Result<void> skip(size_t n) {
        if (pos_ + n > size_) return std::string("skip past end");
        pos_ += n;
        return {};
    }
    Result<void> seek(size_t p) {
        if (p > size_) return std::string("seek past end");
        pos_ = p;
        return {};
    }
    const u8* ptr() const { return data_ + pos_; }

    static constexpr u32 kMaxDimension = 4096;
    static Result<void> validate_dims(u32 w, u32 h) {
        if (w == 0 || h == 0) return std::string("zero dimension");
        if (w > kMaxDimension || h > kMaxDimension) return std::string("dimension exceeds max");
        return {};
    }

private:
    const u8* data_;
    size_t size_;
    size_t pos_;
};

} // namespace browser::image
