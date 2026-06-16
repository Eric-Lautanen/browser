#include "opengl.hpp"
#include <windows.h>
#include <cstring>

namespace browser::platform {

// Load an OpenGL function pointer by name.
// Uses memcpy through a void intermediate to avoid -Wcast-function-type:
// directly casting FARPROC (int (WINAPI*)(...)) to a GL function pointer
// with a different calling convention/signature triggers the warning.
// memcpy of the pointer bytes is well-defined for function pointers
// (std::bit_cast would be ideal but requires C++20 <bit>).
template<typename T>
static T load_gl_func(const char* name) {
    FARPROC proc = wglGetProcAddress(name);
    if (!proc) {
        HMODULE gl_mod = GetModuleHandle("opengl32.dll");
        if (gl_mod) proc = GetProcAddress(gl_mod, name);
    }
    T result;
    memcpy(&result, &proc, sizeof(proc));
    return result;
}

#define LOAD_GL_FUNC(name) \
    name = load_gl_func<decltype(name)>(#name)

// Vertex buffer objects
GLGenBuffersFunc glGenBuffers = nullptr;
GLBindBufferFunc glBindBuffer = nullptr;
GLBufferDataFunc glBufferData = nullptr;
GLBufferSubDataFunc glBufferSubData = nullptr;
GLDeleteBuffersFunc glDeleteBuffers = nullptr;

// Vertex array objects
GLGenVertexArraysFunc glGenVertexArrays = nullptr;
GLBindVertexArrayFunc glBindVertexArray = nullptr;
GLDeleteVertexArraysFunc glDeleteVertexArrays = nullptr;

// Shader objects
GLCreateShaderFunc glCreateShader = nullptr;
GLShaderSourceFunc glShaderSource = nullptr;
GLCompileShaderFunc glCompileShader = nullptr;
GLGetShaderivFunc glGetShaderiv = nullptr;
GLGetShaderInfoLogFunc glGetShaderInfoLog = nullptr;
GLDeleteShaderFunc glDeleteShader = nullptr;
GLAttachShaderFunc glAttachShader = nullptr;
GLLinkProgramFunc glLinkProgram = nullptr;
GLUseProgramFunc glUseProgram = nullptr;
GLCreateProgramFunc glCreateProgram = nullptr;
GLDeleteProgramFunc glDeleteProgram = nullptr;
GLGetProgramivFunc glGetProgramiv = nullptr;
GLGetProgramInfoLogFunc glGetProgramInfoLog = nullptr;

// Vertex attributes
GLGetAttribLocationFunc glGetAttribLocation = nullptr;
GLVertexAttribPointerFunc glVertexAttribPointer = nullptr;
GLEnableVertexAttribArrayFunc glEnableVertexAttribArray = nullptr;
GLDisableVertexAttribArrayFunc glDisableVertexAttribArray = nullptr;

// Uniforms
GLGetUniformLocationFunc glGetUniformLocation = nullptr;
GLUniform1iFunc glUniform1i = nullptr;
GLUniform1fFunc glUniform1f = nullptr;
GLUniformMatrix4fvFunc glUniformMatrix4fv = nullptr;

// Textures
GLGenTexturesFunc glGenTextures = nullptr;
GLBindTextureFunc glBindTexture = nullptr;
GLTexImage2DFunc glTexImage2D = nullptr;
GLTexSubImage2DFunc glTexSubImage2D = nullptr;
GLTexParameteriFunc glTexParameteri = nullptr;
GLDeleteTexturesFunc glDeleteTextures = nullptr;

// Drawing
GLDrawElementsFunc glDrawElements = nullptr;
GLDrawArraysFunc glDrawArrays = nullptr;

// Pixel store
GLPixelStoreiFunc glPixelStorei = nullptr;

// Clear / state
GLClearColorFunc glClearColor = nullptr;
GLClearFunc glClear = nullptr;
GLViewportFunc glViewport = nullptr;

// Texture state
GLActiveTextureFunc glActiveTexture = nullptr;

// Blending / depth
GLEnableFunc glEnable = nullptr;
GLDisableFunc glDisable = nullptr;
GLBlendFuncFunc glBlendFunc = nullptr;
GLScissorFunc glScissor = nullptr;

bool load_opengl_functions() {
    LOAD_GL_FUNC(glGenBuffers);
    LOAD_GL_FUNC(glBindBuffer);
    LOAD_GL_FUNC(glBufferData);
    LOAD_GL_FUNC(glBufferSubData);
    LOAD_GL_FUNC(glDeleteBuffers);

    LOAD_GL_FUNC(glGenVertexArrays);
    LOAD_GL_FUNC(glBindVertexArray);
    LOAD_GL_FUNC(glDeleteVertexArrays);

    LOAD_GL_FUNC(glCreateShader);
    LOAD_GL_FUNC(glShaderSource);
    LOAD_GL_FUNC(glCompileShader);
    LOAD_GL_FUNC(glGetShaderiv);
    LOAD_GL_FUNC(glGetShaderInfoLog);
    LOAD_GL_FUNC(glDeleteShader);
    LOAD_GL_FUNC(glAttachShader);
    LOAD_GL_FUNC(glLinkProgram);
    LOAD_GL_FUNC(glUseProgram);
    LOAD_GL_FUNC(glCreateProgram);
    LOAD_GL_FUNC(glDeleteProgram);
    LOAD_GL_FUNC(glGetProgramiv);
    LOAD_GL_FUNC(glGetProgramInfoLog);

    LOAD_GL_FUNC(glGetAttribLocation);
    LOAD_GL_FUNC(glVertexAttribPointer);
    LOAD_GL_FUNC(glEnableVertexAttribArray);
    LOAD_GL_FUNC(glDisableVertexAttribArray);

    LOAD_GL_FUNC(glGetUniformLocation);
    LOAD_GL_FUNC(glUniform1i);
    LOAD_GL_FUNC(glUniform1f);
    LOAD_GL_FUNC(glUniformMatrix4fv);

    LOAD_GL_FUNC(glGenTextures);
    LOAD_GL_FUNC(glBindTexture);
    LOAD_GL_FUNC(glTexImage2D);
    LOAD_GL_FUNC(glTexSubImage2D);
    LOAD_GL_FUNC(glTexParameteri);
    LOAD_GL_FUNC(glDeleteTextures);

    LOAD_GL_FUNC(glDrawElements);
    LOAD_GL_FUNC(glDrawArrays);

    LOAD_GL_FUNC(glClearColor);
    LOAD_GL_FUNC(glClear);
    LOAD_GL_FUNC(glViewport);

    LOAD_GL_FUNC(glPixelStorei);
    LOAD_GL_FUNC(glActiveTexture);
    LOAD_GL_FUNC(glEnable);
    LOAD_GL_FUNC(glDisable);
    LOAD_GL_FUNC(glBlendFunc);
    LOAD_GL_FUNC(glScissor);

    return true;
}

} // namespace browser::platform
