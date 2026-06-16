#include "window.hpp"
#if defined(_WIN32)
#include "window_win32.hpp"
#else
#error "Unsupported platform"
#endif

namespace browser::platform {

Result<std::unique_ptr<Window>> Window::create_window(const std::string& title, u32 width, u32 height) {
#if defined(_WIN32)
    auto win = std::make_unique<Win32Window>();
    auto r = win->create(title, width, height);
    if (r.is_err()) return r.unwrap_err();
    return std::unique_ptr<Window>(std::move(win));
#else
    return Result<std::unique_ptr<Window>>(std::string("Unsupported platform"));
#endif
}

} // namespace browser::platform
