#pragma once
#include "../tests/utility.hpp"
#include "../render/renderer.hpp"

namespace browser {

enum class ThemeMode { LIGHT, DARK };

struct Theme {
    static Theme light();
    static Theme dark();

    render::Color bg;
    render::Color text;
    render::Color text_secondary;
    render::Color surface;
    render::Color surface_hover;
    render::Color accent;
    render::Color border;
    render::Color tab_active;
    render::Color tab_inactive;
    render::Color bookmark_fill;
    render::Color page_bg;
    f32 shadow_alpha;

    static ThemeMode current;
    static std::vector<ThemeMode> available;
    static void toggle();
};

inline ThemeMode Theme::current = ThemeMode::LIGHT;
inline std::vector<ThemeMode> Theme::available = {ThemeMode::LIGHT, ThemeMode::DARK};

inline Theme Theme::light() {
    return {
        {0.96f, 0.96f, 0.97f, 1.0f},
        {0.08f, 0.08f, 0.10f, 1.0f},
        {0.55f, 0.55f, 0.58f, 1.0f},
        {1.0f,  1.0f,  1.0f,  1.0f},
        {0.92f, 0.92f, 0.94f, 1.0f},
        {0.20f, 0.40f, 0.90f, 1.0f},
        {0.72f, 0.72f, 0.74f, 1.0f},
        {0.94f, 0.94f, 0.97f, 1.0f},
        {0.86f, 0.86f, 0.90f, 1.0f},
        {1.0f,  0.84f, 0.0f,  1.0f},
        {1.0f,  1.0f,  1.0f,  1.0f},
        0.08f
    };
}

inline Theme Theme::dark() {
    return {
        {0.14f, 0.14f, 0.16f, 1.0f},
        {0.92f, 0.92f, 0.94f, 1.0f},
        {0.55f, 0.55f, 0.58f, 1.0f},
        {0.20f, 0.20f, 0.22f, 1.0f},
        {0.26f, 0.26f, 0.28f, 1.0f},
        {0.40f, 0.60f, 1.0f,  1.0f},
        {0.30f, 0.30f, 0.32f, 1.0f},
        {0.22f, 0.22f, 0.24f, 1.0f},
        {0.16f, 0.16f, 0.18f, 1.0f},
        {1.0f,  0.84f, 0.0f,  1.0f},
        {0.12f, 0.12f, 0.14f, 1.0f},
        0.15f
    };
}

inline void Theme::toggle() {
    current = (current == ThemeMode::LIGHT) ? ThemeMode::DARK : ThemeMode::LIGHT;
}

} // namespace browser
