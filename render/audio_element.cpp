#include "audio_element.hpp"
#include "../html/dom.hpp"

namespace browser::render {

AudioElement::AudioElement() = default;

bool AudioElement::load(const std::string& url, const u8* data, u32 size) {
    src_ = url;
    auto buf = std::make_shared<platform::AudioBuffer>();
    auto r = buf->load_wav(data, size);
    if (r.is_err()) return false;
    auto r2 = player_.play(buf, looping());
    return r2.is_ok();
}

void AudioElement::play() {
    if (player_.state() == platform::AudioState::PAUSED)
        player_.resume();
    else if (player_.state() != platform::AudioState::PLAYING)
        player_.play(std::make_shared<platform::AudioBuffer>()); // placeholder
}

void AudioElement::pause() {
    player_.pause();
}

void AudioElement::stop() {
    player_.stop();
}

f32 AudioElement::current_time() const {
    return player_.current_time();
}

void AudioElement::set_current_time(f32 t) {
    player_.set_current_time(t);
}

f32 AudioElement::duration() const {
    return player_.duration();
}

AudioManager& AudioManager::instance() {
    static AudioManager mgr;
    return mgr;
}

AudioElement* AudioManager::get_or_create(html::Element* el) {
    auto it = elements_.find(el);
    if (it != elements_.end()) return it->second.get();
    auto ae = std::make_unique<AudioElement>();
    auto* ptr = ae.get();
    elements_[el] = std::move(ae);
    return ptr;
}

void AudioManager::remove(html::Element* el) {
    elements_.erase(el);
}

void AudioManager::clear() {
    elements_.clear();
}

} // namespace browser::render
