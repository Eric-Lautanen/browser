#include "window_win32.hpp"
#include <windowsx.h>
#include <GL/gl.h>
#include <GL/wglext.h>

namespace browser::platform {

namespace {

const char* kClassName = "BrowserWin32Window";
const char* kTempClassName = "BrowserTempWindow";

using wglCreateContextAttribsARBProc = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using wglChoosePixelFormatARBProc = BOOL (WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);

wglCreateContextAttribsARBProc wglCreateContextAttribsARB = nullptr;
wglChoosePixelFormatARBProc wglChoosePixelFormatARB = nullptr;

bool init_wgl_extensions() {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kTempClassName;
    RegisterClassEx(&wc);

    HWND temp_hwnd = CreateWindowEx(0, kTempClassName, nullptr, WS_POPUP,
                                    0, 0, 1, 1, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!temp_hwnd) {
        UnregisterClass(kTempClassName, GetModuleHandle(nullptr));
        return false;
    }

    HDC temp_hdc = GetDC(temp_hwnd);
    if (!temp_hdc) {
        DestroyWindow(temp_hwnd);
        UnregisterClass(kTempClassName, GetModuleHandle(nullptr));
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(temp_hdc, &pfd);
    if (!pf || !SetPixelFormat(temp_hdc, pf, &pfd)) {
        ReleaseDC(temp_hwnd, temp_hdc);
        DestroyWindow(temp_hwnd);
        UnregisterClass(kTempClassName, GetModuleHandle(nullptr));
        return false;
    }

    HGLRC temp_ctx = wglCreateContext(temp_hdc);
    if (!temp_ctx || !wglMakeCurrent(temp_hdc, temp_ctx)) {
        if (temp_ctx) wglDeleteContext(temp_ctx);
        ReleaseDC(temp_hwnd, temp_hdc);
        DestroyWindow(temp_hwnd);
        UnregisterClass(kTempClassName, GetModuleHandle(nullptr));
        return false;
    }

    PROC proc1 = wglGetProcAddress("wglCreateContextAttribsARB");
    PROC proc2 = wglGetProcAddress("wglChoosePixelFormatARB");
    wglCreateContextAttribsARB = reinterpret_cast<wglCreateContextAttribsARBProc>(reinterpret_cast<void*>(proc1));
    wglChoosePixelFormatARB = reinterpret_cast<wglChoosePixelFormatARBProc>(reinterpret_cast<void*>(proc2));

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temp_ctx);
    ReleaseDC(temp_hwnd, temp_hdc);
    DestroyWindow(temp_hwnd);
    UnregisterClass(kTempClassName, GetModuleHandle(nullptr));

    return (wglCreateContextAttribsARB != nullptr && wglChoosePixelFormatARB != nullptr);
}

bool setup_pixel_format_arb(HDC hdc) {
    const int pixel_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, 1,
        WGL_SUPPORT_OPENGL_ARB, 1,
        WGL_DOUBLE_BUFFER_ARB, 1,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_DEPTH_BITS_ARB, 24,
        WGL_STENCIL_BITS_ARB, 8,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        0
    };

    int pixel_format = 0;
    UINT num_formats = 0;
    if (!wglChoosePixelFormatARB(hdc, pixel_attribs, nullptr, 1, &pixel_format, &num_formats)) {
        return false;
    }
    if (num_formats == 0) return false;

    PIXELFORMATDESCRIPTOR pfd = {};
    return DescribePixelFormat(hdc, pixel_format, sizeof(pfd), &pfd) != 0 &&
           SetPixelFormat(hdc, pixel_format, &pfd);
}

KeyCode vk_to_keycode(WPARAM wparam) {
    switch (wparam) {
        case VK_ESCAPE: return KeyCode::ESCAPE;
        case VK_RETURN: return KeyCode::ENTER;
        case VK_LEFT:   return KeyCode::LEFT;
        case VK_RIGHT:  return KeyCode::RIGHT;
        case VK_UP:     return KeyCode::UP;
        case VK_DOWN:   return KeyCode::DOWN;
        case VK_HOME:   return KeyCode::HOME;
        case VK_END:    return KeyCode::END;
        case VK_PRIOR:  return KeyCode::PAGE_UP;
        case VK_NEXT:   return KeyCode::PAGE_DOWN;
        case VK_INSERT: return KeyCode::INSERT;
        case VK_CAPITAL: return KeyCode::CAPS_LOCK;
        case VK_BACK:   return KeyCode::BACKSPACE;
        case VK_DELETE: return KeyCode::DELETE;
        case VK_TAB:    return KeyCode::TAB;
        case VK_SHIFT:  return KeyCode::SHIFT;
        case VK_CONTROL: return KeyCode::CTRL;
        case VK_MENU:   return KeyCode::ALT;
        case VK_SPACE:  return KeyCode::SPACE;
        case VK_LSHIFT: return KeyCode::LSHIFT;
        case VK_RSHIFT: return KeyCode::RSHIFT;
        case VK_LCONTROL: return KeyCode::LCTRL;
        case VK_RCONTROL: return KeyCode::RCTRL;
        case VK_LMENU:  return KeyCode::LALT;
        case VK_RMENU:  return KeyCode::RALT;
        case VK_LWIN: case VK_RWIN: return KeyCode::SUPER;
        case '0': return KeyCode::_0; case '1': return KeyCode::_1;
        case '2': return KeyCode::_2; case '3': return KeyCode::_3;
        case '4': return KeyCode::_4; case '5': return KeyCode::_5;
        case '6': return KeyCode::_6; case '7': return KeyCode::_7;
        case '8': return KeyCode::_8; case '9': return KeyCode::_9;
        case 'A': return KeyCode::A; case 'B': return KeyCode::B;
        case 'C': return KeyCode::C; case 'D': return KeyCode::D;
        case 'E': return KeyCode::E; case 'F': return KeyCode::F;
        case 'G': return KeyCode::G; case 'H': return KeyCode::H;
        case 'I': return KeyCode::I; case 'J': return KeyCode::J;
        case 'K': return KeyCode::K; case 'L': return KeyCode::L;
        case 'M': return KeyCode::M; case 'N': return KeyCode::N;
        case 'O': return KeyCode::O; case 'P': return KeyCode::P;
        case 'Q': return KeyCode::Q; case 'R': return KeyCode::R;
        case 'S': return KeyCode::S; case 'T': return KeyCode::T;
        case 'U': return KeyCode::U; case 'V': return KeyCode::V;
        case 'W': return KeyCode::W; case 'X': return KeyCode::X;
        case 'Y': return KeyCode::Y; case 'Z': return KeyCode::Z;
        case VK_OEM_PERIOD: return KeyCode::PERIOD;
        case VK_OEM_COMMA:  return KeyCode::COMMA;
        case 0xBF:          return KeyCode::SLASH;
        case VK_OEM_1:      return KeyCode::SEMICOLON;
        case VK_OEM_7:      return KeyCode::QUOTE;
        case VK_OEM_5:      return KeyCode::BACKSLASH;
        case VK_OEM_MINUS:  return KeyCode::MINUS;
        case VK_OEM_PLUS:   return KeyCode::EQUALS;
        case VK_OEM_4:      return KeyCode::LBRACKET;
        case VK_OEM_6:      return KeyCode::RBRACKET;
        case VK_OEM_3:      return KeyCode::BACKTICK;
        case VK_F1:  return KeyCode::F1;  case VK_F2:  return KeyCode::F2;
        case VK_F3:  return KeyCode::F3;  case VK_F4:  return KeyCode::F4;
        case VK_F5:  return KeyCode::F5;  case VK_F6:  return KeyCode::F6;
        case VK_F7:  return KeyCode::F7;  case VK_F8:  return KeyCode::F8;
        case VK_F9:  return KeyCode::F9;  case VK_F10: return KeyCode::F10;
        case VK_F11: return KeyCode::F11; case VK_F12: return KeyCode::F12;
        default: return KeyCode::UNKNOWN;
    }
}

} // anonymous namespace

Win32Window::Win32Window() {
    register_window_class();
}

Result<void> Win32Window::create(const std::string& title, u32 width, u32 height) {
    extent_ = {width, height};

    hwnd_ = CreateWindowEx(
        0, kClassName, title.c_str(),
        WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (int)width, (int)height,
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!hwnd_) {
        return Result<void>(std::string("CreateWindowEx failed"));
    }

    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return Result<void>(std::string("GetDC failed"));
    }

    if (!init_wgl_extensions()) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return Result<void>(std::string("Failed to load WGL extensions"));
    }

    if (!setup_pixel_format_arb(hdc_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return Result<void>(std::string("Failed to set pixel format"));
    }

    const int context_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    hglrc_ = wglCreateContextAttribsARB(hdc_, 0, context_attribs);
    if (!hglrc_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return Result<void>(std::string("wglCreateContextAttribsARB failed"));
    }

    SetWindowLongPtr(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

    return {};
}

void Win32Window::show() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

bool Win32Window::pump_events() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void Win32Window::swap_buffers() {
    SwapBuffers(hdc_);
}

void Win32Window::make_context_current() {
    wglMakeCurrent(hdc_, hglrc_);
}

Win32Window::~Win32Window() {
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
    }
    if (hdc_ && hwnd_) {
        ReleaseDC(hwnd_, hdc_);
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

void Win32Window::set_fullscreen(bool fullscreen) {
    if (fullscreen == fullscreen_) return;
    fullscreen_ = fullscreen;

    if (fullscreen) {
        // Save current state
        GetWindowRect(hwnd_, &saved_rect_);
        saved_style_ = GetWindowLong(hwnd_, GWL_STYLE);

        // Remove titlebar and borders
        SetWindowLong(hwnd_, GWL_STYLE, saved_style_ & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX));

        // Get monitor info
        HMONITOR hMonitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &mi);

        SetWindowPos(hwnd_, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER);
    } else {
        // Restore previous state
        SetWindowLong(hwnd_, GWL_STYLE, saved_style_);
        SetWindowPos(hwnd_, HWND_TOP,
                     saved_rect_.left, saved_rect_.top,
                     saved_rect_.right - saved_rect_.left,
                     saved_rect_.bottom - saved_rect_.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER);
    }
}

void Win32Window::register_window_class() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;

    RegisterClassEx(&wc);
    registered = true;
}

LRESULT CALLBACK Win32Window::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto win = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            if (win) win->set_should_close(true);
            return 0;

        case WM_SIZE:
            if (win) {
                win->extent_.width = (u32)GET_X_LPARAM(lparam);
                win->extent_.height = (u32)GET_Y_LPARAM(lparam);
                Event e;
                e.type = Event::Type::WINDOW_RESIZE;
                e.width = win->extent_.width;
                e.height = win->extent_.height;
                win->dispatch_event(e);
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (win) {
                Event e;
                e.type = Event::Type::KEY_DOWN;
                e.key = vk_to_keycode(wparam);
                win->dispatch_event(e);
            }
            return 0;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (win) {
                Event e;
                e.type = Event::Type::KEY_UP;
                e.key = vk_to_keycode(wparam);
                win->dispatch_event(e);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (win) {
                Event e;
                e.type = Event::Type::MOUSE_MOVE;
                e.mouse_x = GET_X_LPARAM(lparam);
                e.mouse_y = GET_Y_LPARAM(lparam);
                win->dispatch_event(e);
            }
            return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (win) {
                Event e;
                e.type = Event::Type::MOUSE_DOWN;
                e.button = (msg == WM_LBUTTONDOWN) ? MouseButton::LEFT :
                           (msg == WM_RBUTTONDOWN) ? MouseButton::RIGHT :
                                                     MouseButton::MIDDLE;
                e.mouse_x = GET_X_LPARAM(lparam);
                e.mouse_y = GET_Y_LPARAM(lparam);
                win->dispatch_event(e);
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if (win) {
                Event e;
                e.type = Event::Type::MOUSE_UP;
                e.button = (msg == WM_LBUTTONUP) ? MouseButton::LEFT :
                           (msg == WM_RBUTTONUP) ? MouseButton::RIGHT :
                                                   MouseButton::MIDDLE;
                e.mouse_x = GET_X_LPARAM(lparam);
                e.mouse_y = GET_Y_LPARAM(lparam);
                win->dispatch_event(e);
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (win) {
                Event e;
                e.type = Event::Type::MOUSE_SCROLL;
                e.scroll_delta = (i32)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
                win->dispatch_event(e);
            }
            return 0;

        case WM_DESTROY:
            if (win) {
                Event e;
                e.type = Event::Type::WINDOW_CLOSE;
                win->dispatch_event(e);
            }
            return 0;

        case WM_NCCALCSIZE:
            if (wparam) {
                if (IsZoomed(hwnd)) {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    int border = GetSystemMetrics(SM_CXSIZEFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left   += border;
                    params->rgrc[0].right  -= border;
                    params->rgrc[0].top    += border;
                    params->rgrc[0].bottom -= border;
                }
                return 0;
            }
            break;

        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hwnd, msg, wparam, lparam);
            if (hit == HTCLIENT) {
                POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                ScreenToClient(hwnd, &pt);
                if (win && win->nchittest_callback_) {
                    LRESULT custom = win->nchittest_callback_(pt.x, pt.y);
                    if (custom != HTCLIENT) return custom;
                }
            }
            return hit;
        }

        case WM_ACTIVATE:
            if (win) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

} // namespace browser::platform
