#include "opengl.hpp"
#include <windows.h>
#include <bit>
#include <cstring>

namespace browser::platform {

// Load an OpenGL function pointer by name. Uses std::bit_cast (C++20).
template<typename T>
static T load_gl_func(const char* name) {
    FARPROC proc = wglGetProcAddress(name);
    if (!proc) {
        HMODULE gl_mod = GetModuleHandle("opengl32.dll");
        if (gl_mod) proc = GetProcAddress(gl_mod, name);
    }
    return std::bit_cast<T>(proc);
}

#define LOAD_GL_FUNC(name) \
    name = load_gl_func<decltype(name)>(#name)

#define X(n, t) t n = nullptr;
BROWSER_GL_FUNCS(X)
#undef X

bool load_opengl_functions() {
#define X(n, t) LOAD_GL_FUNC(n);
    BROWSER_GL_FUNCS(X)
#undef X

    // P-M7: verify required entry points — stub driver yields null pointers
    if (!glGenBuffers || !glBindBuffer || !glBufferData || !glCreateShader || !glCreateProgram ||
        !glGetUniformLocation || !glGenTextures || !glDrawElements || !glViewport) {
        return false;
    }
    return true;
}

} // namespace browser::platform
