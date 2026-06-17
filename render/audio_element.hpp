#pragma once
#include "../tests/utility.hpp"
#include "../platform/audio.hpp"
#include <string>
#include <memory>
#include <unordered_map>

namespace browser::html {
struct Element;
}

namespace browser::render {

class AudioElement {
public:
    AudioElement();
    ~AudioElement() = default;

    bool load(const std::string& url, const u8* data, u32 size);
    void play();
    void pause();
    void stop();

    f32 current_time() const;
    void set_current_time(f32 t);
    f32 duration() const;
    f32 volume() const { return player_.volume(); }
    void set_volume(f32 v) { player_.set_volume(v); }
    bool paused() const { return player_.state() == platform::AudioState::PAUSED || player_.state() == platform::AudioState::STOPPED; }
    bool ended() const { return false; }
    bool looping() const { return player_.looping(); }
    void set_looping(bool l) { player_.set_looping(l); }

    const std::string& src() const { return src_; }
    void set_src(const std::string& s) { src_ = s; }

private:
    platform::AudioPlayer player_;
    std::string src_;
};

class AudioManager {
public:
    static AudioManager& instance();
    AudioElement* get_or_create(html::Element* el);
    void remove(html::Element* el);
    void clear();

private:
    std::unordered_map<html::Element*, std::unique_ptr<AudioElement>> elements_;
};

} // namespace browser::render
