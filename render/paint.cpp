#include "paint.hpp"

namespace browser::render {

void DisplayList::push(const PaintCommand& cmd) {
    commands_.push_back(cmd);
}

void DisplayList::clear() {
    commands_.clear();
}

const std::vector<PaintCommand>& DisplayList::commands() const {
    return commands_;
}

} // namespace browser::render
