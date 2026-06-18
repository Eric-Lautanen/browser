#pragma once
#include <windows.h>
#include "window.hpp"

namespace browser::platform {

class Win32Window : public Window {
public:
    Win32Window();
    Result<void> create(const std::string& title, u32 width, u32 height) override;
    void show() override;
    bool pump_events() override;
    void swap_buffers() override;
    void make_context_current() override;
    Extent get_extent() const override { return extent_; }
    void* get_native_handle() const override { return (void*)hwnd_; }
    ~Win32Window() override;

    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const { return fullscreen_; }
    void toggle_maximize();

public:
    std::function<LRESULT(i32, i32)> nchittest_callback_;
    bool is_active() const override { return GetActiveWindow() == hwnd_; }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    Extent extent_{800, 600};
    bool fullscreen_ = false;
    bool maximized_ = false;
    RECT saved_rect_{};
    RECT pre_maximize_rect_{};
    LONG saved_style_ = 0;

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void register_window_class();
};

} // namespace browser::platform
