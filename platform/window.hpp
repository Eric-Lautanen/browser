#pragma once
#include <string>
#include <functional>
#include <memory>
#include "../tests/utility.hpp"

// windows.h defines DELETE as a macro; undefine to avoid conflict with KeyCode enum
#ifdef DELETE
#undef DELETE
#endif

namespace browser::platform {

struct Extent { u32 width; u32 height; };

enum class KeyCode {
    UNKNOWN, ESCAPE, ENTER, LEFT, RIGHT, UP, DOWN,
    HOME, END, PAGE_UP, PAGE_DOWN, INSERT, CAPS_LOCK,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    BACKSPACE, DELETE, TAB, SHIFT, CTRL, ALT, SPACE,
    LSHIFT, RSHIFT, LCTRL, RCTRL, LALT, RALT, SUPER,
    PERIOD, COMMA, SLASH, SEMICOLON, QUOTE, BACKSLASH,
    MINUS, EQUALS, LBRACKET, RBRACKET, BACKTICK
};

enum class MouseButton { NONE, LEFT, RIGHT, MIDDLE };

struct Event {
    enum class Type { NONE, KEY_DOWN, KEY_UP, MOUSE_MOVE, MOUSE_DOWN, MOUSE_UP,
                      MOUSE_SCROLL, WINDOW_CLOSE, WINDOW_RESIZE, WINDOW_MOVE, QUIT };
    Type type;
    KeyCode key;
    MouseButton button;
    i32 mouse_x, mouse_y;
    i32 scroll_delta;
    u32 width, height;
};

using EventCallback = std::function<void(const Event&)>;

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) noexcept = default;
    Window& operator=(Window&&) noexcept = default;
    virtual ~Window() = default;

    virtual Result<void> create(const std::string& title, u32 width, u32 height) = 0;
    virtual void show() = 0;
    virtual bool pump_events() = 0;
    virtual void swap_buffers() = 0;
    virtual void make_context_current() = 0;
    virtual Extent get_extent() const = 0;
    virtual void* get_native_handle() const = 0;

    void set_event_callback(EventCallback cb) { event_callback_ = std::move(cb); }
    static Result<std::unique_ptr<Window>> create_window(const std::string& title, u32 width, u32 height);
    bool should_close() const { return should_close_; }
    void set_should_close(bool v) { should_close_ = v; }
    virtual bool is_active() const { return true; }

protected:
    EventCallback event_callback_;
    bool should_close_ = false;
    void dispatch_event(const Event& e) { if (event_callback_) event_callback_(e); }
};

} // namespace browser::platform
