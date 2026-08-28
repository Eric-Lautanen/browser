#pragma once
#include "../../css/animation.hpp"
#include <string>
#include <unordered_map>

namespace browser {

// Wave 4: AnimationCoordinator extracted from BrowserWindow (window.cpp:498)
// Owns per-element animation state and drives apply_animation_values()
struct AnimationCoordinator {
    void setup(const std::unordered_map<std::string, std::string>& page_css, void* dom_root);
    void tick(float dt, void* window);
    void apply(void* window);
};

} // namespace browser
