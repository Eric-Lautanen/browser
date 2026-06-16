#include "texture.hpp"
#include "../platform/opengl.hpp"

namespace browser::render {

namespace pgl = browser::platform;

Texture2D::Texture2D() = default;

Texture2D::~Texture2D() {
    if (texture_id_) {
        pgl::glDeleteTextures(1, &texture_id_);
    }
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : texture_id_(other.texture_id_)
    , width_(other.width_)
    , height_(other.height_) {
    other.texture_id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        if (texture_id_) pgl::glDeleteTextures(1, &texture_id_);
        texture_id_ = other.texture_id_;
        width_ = other.width_;
        height_ = other.height_;
        other.texture_id_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

Result<void> Texture2D::create(u32 width, u32 height, const u8* data) {
    if (texture_id_) {
        pgl::glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }

    pgl::glGenTextures(1, &texture_id_);
    if (!texture_id_) {
        return Result<void>(std::string("glGenTextures failed"));
    }

    pgl::glBindTexture(GL_TEXTURE_2D, texture_id_);
    pgl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pgl::glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, (GLsizei)width, (GLsizei)height,
                      0, GL_RED, GL_UNSIGNED_BYTE, data);
    pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    pgl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    width_ = width;
    height_ = height;
    return {};
}

void Texture2D::update_sub(u32 x, u32 y, u32 width, u32 height, const u8* data) {
    if (!texture_id_) return;
    pgl::glBindTexture(GL_TEXTURE_2D, texture_id_);
    pgl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pgl::glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height, GL_RED, GL_UNSIGNED_BYTE, data);
}

void Texture2D::bind(u32 slot) const {
    if (texture_id_) {
        pgl::glActiveTexture(GL_TEXTURE0 + slot);
        pgl::glBindTexture(GL_TEXTURE_2D, texture_id_);
    }
}

void Texture2D::unbind() const {
    pgl::glBindTexture(GL_TEXTURE_2D, 0);
}

}
