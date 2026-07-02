#include "offscreen_target.hpp"
#include "texture.hpp"
#include "../platform/opengl.hpp"
#include <vector>

namespace browser::render {

namespace pgl = browser::platform;

OffscreenTarget::OffscreenTarget() = default;

OffscreenTarget::~OffscreenTarget() {
    if (fbo_id_) {
        pgl::glDeleteFramebuffers(1, &fbo_id_);
    }
}

Result<void> OffscreenTarget::create(u32 width, u32 height) {
    if (fbo_id_) {
        pgl::glDeleteFramebuffers(1, &fbo_id_);
        fbo_id_ = 0;
    }

    // Create color texture
    texture_ = std::make_unique<Texture2D>();
    std::vector<u8> empty(width * height * 4, 0);
    auto r = texture_->create(width, height, empty.data(), true);
    if (r.is_err()) return r;

    // Create FBO
    pgl::glGenFramebuffers(1, &fbo_id_);
    if (!fbo_id_) return Result<void>(std::string("glGenFramebuffers failed"));

    pgl::glBindFramebuffer(GL_FRAMEBUFFER, fbo_id_);
    pgl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_->id(), 0);

    GLenum status = pgl::glCheckFramebufferStatus(GL_FRAMEBUFFER);
    pgl::glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        pgl::glDeleteFramebuffers(1, &fbo_id_);
        fbo_id_ = 0;
        return Result<void>(std::string("Framebuffer incomplete: ") + std::to_string(status));
    }

    width_ = width;
    height_ = height;
    return {};
}

void OffscreenTarget::bind() {
    pgl::glBindFramebuffer(GL_FRAMEBUFFER, fbo_id_);
}

void OffscreenTarget::unbind() {
    pgl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u32 OffscreenTarget::texture_id() const {
    return texture_ ? texture_->id() : 0;
}

}  // namespace browser::render
