#pragma once
#include "../core/utility.hpp"
#include <memory>

namespace browser::render {

class Texture2D;

// Simple offscreen render target (FBO + color texture)
class OffscreenTarget {
public:
    OffscreenTarget();
    ~OffscreenTarget();
    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    Result<void> create(u32 width, u32 height);
    void bind();
    void unbind();
    u32 texture_id() const;
    u32 width() const { return width_; }
    u32 height() const { return height_; }

private:
    u32 fbo_id_ = 0;
    u32 width_ = 0, height_ = 0;
    std::unique_ptr<Texture2D> texture_;
};

}  // namespace browser::render
