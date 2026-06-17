#include "video_element.hpp"
#include "../html/dom.hpp"

namespace browser::render {

VideoElement::VideoElement() = default;

bool VideoElement::load(const std::string& url, const u8*, u32) {
    src_ = url;
    return false;
}

void VideoElement::play() {}
void VideoElement::pause() {}
void VideoElement::stop() {}

VideoManager& VideoManager::instance() {
    static VideoManager mgr;
    return mgr;
}

VideoElement* VideoManager::get_or_create(html::Element* el) {
    auto it = elements_.find(el);
    if (it != elements_.end()) return it->second.get();
    auto ve = std::make_unique<VideoElement>();
    auto* ptr = ve.get();
    elements_[el] = std::move(ve);
    return ptr;
}

void VideoManager::remove(html::Element* el) {
    elements_.erase(el);
}

void VideoManager::clear() {
    elements_.clear();
}

} // namespace browser::render
