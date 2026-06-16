#include "test_framework.hpp"
#include "utility.hpp"
#include "../platform/window.hpp"

TEST(window_create_destroy, {
    auto result = browser::platform::Window::create_window("Test", 640, 480);
    if (result.is_err()) {
        return true;
    }
    auto& window = result.unwrap();
    auto ext = window->get_extent();
    ASSERT(ext.width > 0);
    ASSERT(ext.height > 0);
    window->set_should_close(true);
    ASSERT(window->should_close());
})
