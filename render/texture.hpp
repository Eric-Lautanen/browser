#pragma once
#include "../tests/utility.hpp"

namespace browser::render {

class Texture2D {
public:
    Texture2D(); ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D(Texture2D&&) noexcept;
    Texture2D& operator=(Texture2D&&) noexcept;

    Result<void> create(u32 width, u32 height, const u8* data, bool use_rgba = false);
    void update_sub(u32 x, u32 y, u32 width, u32 height, const u8* data, bool use_rgba = false);
    void bind(u32 slot = 0) const;
    void unbind() const;
    u32 id() const { return texture_id_; }
    u32 width() const { return width_; }
    u32 height() const { return height_; }
    bool is_rgba() const { return is_rgba_; }

private:
    u32 texture_id_ = 0;
    u32 width_ = 0, height_ = 0;
    bool is_rgba_ = false;
};

}
