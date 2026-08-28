#pragma once
#include <chrono>
#include <functional>

namespace browser {

// Wave 4: FrameLoop extracted from BrowserWindow::run() (window.cpp:271)
// First step: interface boundary; full message-wait/blink/session/resize/render loop moves next batch
struct FrameLoop {
    using Clock = std::chrono::steady_clock;
    static constexpr int kAnimFrameMs = 16;
    static constexpr int kCaretBlinkMs = 60;
    static constexpr int kIdleDozeMs = 200;
    static int idle_wait_ms(bool animating, bool caret_active);
    static void pump_pending(void* browser_window);
};

} // namespace browser
