#pragma once
#include "../tests/utility.hpp"
#include "../platform/audio.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace browser::html {
struct Element;
}

namespace browser::render {

class VideoElement {
public:
    VideoElement();
    ~VideoElement() = default;

    bool load(const std::string& url, const u8* data, u32 size);
    void play();
    void pause();
    void stop();

    f32 current_time() const { return 0; }
    void set_current_time(f32) {}
    f32 duration() const { return 0; }
    f32 volume() const { return 0; }
    void set_volume(f32) {}
    bool paused() const { return true; }
    bool ended() const { return false; }
    bool looping() const { return false; }
    void set_looping(bool) {}

    u32 video_width() const { return 0; }
    u32 video_height() const { return 0; }

    const std::string& src() const { return src_; }
    void set_src(const std::string& s) { src_ = s; }

    const u8* current_frame() const { return nullptr; }
    u32 frame_width() const { return 0; }
    u32 frame_height() const { return 0; }

private:
    std::string src_;
};

class VideoManager {
public:
    static VideoManager& instance();
    VideoElement* get_or_create(html::Element* el);
    void remove(html::Element* el);
    void clear();

private:
    std::unordered_map<html::Element*, std::unique_ptr<VideoElement>> elements_;
};

} // namespace browser::render
