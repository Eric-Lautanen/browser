#include "frame_loop.hpp"

namespace browser {

int FrameLoop::idle_wait_ms(bool animating, bool caret_active) {
    if (animating) return kAnimFrameMs;
    if (caret_active) return kCaretBlinkMs;
    return kIdleDozeMs;
}

void FrameLoop::pump_pending(void* /*browser_window*/) {
    // Stub — will call BrowserWindow::absorb_loaded_pages() + pending navigation drain
}

} // namespace browser
