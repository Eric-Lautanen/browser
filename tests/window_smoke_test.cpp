#include "test_framework.hpp"
#include "utility.hpp"
#include "../platform/window.hpp"
#include <cstdlib>

TEST(window_create_destroy, {
    auto result = browser::platform::Window::create_window("Test", 640, 480);
    if (result.is_err()) {
        // Headless CI has no display; allow opt-out but otherwise fail.
        const char* headless = std::getenv("BROWSER_HEADLESS");
        if (headless && std::string(headless) == "1")
            return true;
        ASSERT(result.is_ok());
        return false;
    }
    auto& window = result.unwrap();
    auto ext = window->get_extent();
    ASSERT(ext.width > 0);
    ASSERT(ext.height > 0);
    window->set_should_close(true);
    ASSERT(window->should_close());
})
