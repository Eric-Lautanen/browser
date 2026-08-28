#pragma once
#include "../core/utility.hpp"
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstddef>

// MinGW's gl.h is missing some modern GL types; define them if absent
#ifndef GLchar
typedef char GLchar;
#endif
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef GLintptr
typedef ptrdiff_t GLintptr;
#endif
#ifndef GLvoid
typedef void GLvoid;
#endif
#ifndef GLclampf
typedef GLfloat GLclampf;
#endif

// MinGW's gl.h is missing FBO-related constants; define them if absent
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

namespace browser::platform {

// NOTE: The extern declarations below (glGenBuffers, glGenTextures, etc.)
// are function pointer variables in namespace browser::platform, NOT the
// GL 1.1 entry points from opengl32.dll.  MinGW's <GL/gl.h> declares the
// real GL functions in the global namespace with the same names, so there
// is no ODR conflict — the two live in different namespaces.  The
// LOAD_GL_FUNC macro resolves decltype() against our namespace-local
// function pointer type, while the string passed to wglGetProcAddress
// matches the GL entry point name.

bool load_opengl_functions();

// Custom function pointer type aliases (MinGW glext.h lacks core GL-version-independent types)
using GLGenBuffersFunc = void (APIENTRY*)(GLsizei, GLuint*);
using GLBindBufferFunc = void (APIENTRY*)(GLenum, GLuint);
using GLBufferDataFunc = void (APIENTRY*)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
using GLBufferSubDataFunc = void (APIENTRY*)(GLenum, GLintptr, GLsizeiptr, const GLvoid*);
using GLDeleteBuffersFunc = void (APIENTRY*)(GLsizei, const GLuint*);

using GLGenVertexArraysFunc = void (APIENTRY*)(GLsizei, GLuint*);
using GLBindVertexArrayFunc = void (APIENTRY*)(GLuint);
using GLDeleteVertexArraysFunc = void (APIENTRY*)(GLsizei, const GLuint*);

using GLCreateShaderFunc = GLuint (APIENTRY*)(GLenum);
using GLShaderSourceFunc = void (APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GLCompileShaderFunc = void (APIENTRY*)(GLuint);
using GLGetShaderivFunc = void (APIENTRY*)(GLuint, GLenum, GLint*);
using GLGetShaderInfoLogFunc = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GLDeleteShaderFunc = void (APIENTRY*)(GLuint);
using GLAttachShaderFunc = void (APIENTRY*)(GLuint, GLuint);
using GLLinkProgramFunc = void (APIENTRY*)(GLuint);
using GLUseProgramFunc = void (APIENTRY*)(GLuint);
using GLCreateProgramFunc = GLuint (APIENTRY*)();
using GLDeleteProgramFunc = void (APIENTRY*)(GLuint);
using GLGetProgramivFunc = void (APIENTRY*)(GLuint, GLenum, GLint*);
using GLGetProgramInfoLogFunc = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);

using GLGetAttribLocationFunc = GLint (APIENTRY*)(GLuint, const GLchar*);
using GLVertexAttribPointerFunc = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
using GLEnableVertexAttribArrayFunc = void (APIENTRY*)(GLuint);
using GLDisableVertexAttribArrayFunc = void (APIENTRY*)(GLuint);

using GLGetUniformLocationFunc = GLint (APIENTRY*)(GLuint, const GLchar*);
using GLUniform1iFunc = void (APIENTRY*)(GLint, GLint);
using GLUniform1fFunc = void (APIENTRY*)(GLint, GLfloat);
using GLUniformMatrix4fvFunc = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);

using GLGenTexturesFunc = void (APIENTRY*)(GLsizei, GLuint*);
using GLBindTextureFunc = void (APIENTRY*)(GLenum, GLuint);
using GLTexImage2DFunc = void (APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
using GLTexSubImage2DFunc = void (APIENTRY*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
using GLTexParameteriFunc = void (APIENTRY*)(GLenum, GLenum, GLint);
using GLDeleteTexturesFunc = void (APIENTRY*)(GLsizei, const GLuint*);

using GLDrawElementsFunc = void (APIENTRY*)(GLenum, GLsizei, GLenum, const GLvoid*);
using GLDrawArraysFunc = void (APIENTRY*)(GLenum, GLint, GLsizei);

using GLClearColorFunc = void (APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
using GLClearFunc = void (APIENTRY*)(GLbitfield);
using GLViewportFunc = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);

using GLActiveTextureFunc = void (APIENTRY*)(GLenum);
using GLEnableFunc = void (APIENTRY*)(GLenum);
using GLDisableFunc = void (APIENTRY*)(GLenum);
using GLIsEnabledFunc = GLboolean(APIENTRY *)(GLenum);
using GLGetErrorFunc = GLenum(APIENTRY *)(void);
using GLGetIntegerVFunc = void(APIENTRY *)(GLenum, GLint *);
using GLBlendFuncFunc = void (APIENTRY*)(GLenum, GLenum);
using GLScissorFunc = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GLUniform2fFunc = void(APIENTRY *)(GLint, GLfloat, GLfloat);

// Framebuffer objects
using GLGenFramebuffersFunc = void (APIENTRY*)(GLsizei, GLuint*);
using GLDeleteFramebuffersFunc = void (APIENTRY*)(GLsizei, const GLuint*);
using GLBindFramebufferFunc = void (APIENTRY*)(GLenum, GLuint);
using GLFramebufferTexture2DFunc = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GLCheckFramebufferStatusFunc = GLenum (APIENTRY*)(GLenum);

// X-macro list for all GL entry points — collapses extern/definition/loader triple (P-M7)
#define BROWSER_GL_FUNCS(X) \
    X(glGenBuffers, GLGenBuffersFunc) \
    X(glBindBuffer, GLBindBufferFunc) \
    X(glBufferData, GLBufferDataFunc) \
    X(glBufferSubData, GLBufferSubDataFunc) \
    X(glDeleteBuffers, GLDeleteBuffersFunc) \
    X(glGenVertexArrays, GLGenVertexArraysFunc) \
    X(glBindVertexArray, GLBindVertexArrayFunc) \
    X(glDeleteVertexArrays, GLDeleteVertexArraysFunc) \
    X(glCreateShader, GLCreateShaderFunc) \
    X(glShaderSource, GLShaderSourceFunc) \
    X(glCompileShader, GLCompileShaderFunc) \
    X(glGetShaderiv, GLGetShaderivFunc) \
    X(glGetShaderInfoLog, GLGetShaderInfoLogFunc) \
    X(glDeleteShader, GLDeleteShaderFunc) \
    X(glAttachShader, GLAttachShaderFunc) \
    X(glLinkProgram, GLLinkProgramFunc) \
    X(glUseProgram, GLUseProgramFunc) \
    X(glCreateProgram, GLCreateProgramFunc) \
    X(glDeleteProgram, GLDeleteProgramFunc) \
    X(glGetProgramiv, GLGetProgramivFunc) \
    X(glGetProgramInfoLog, GLGetProgramInfoLogFunc) \
    X(glGetAttribLocation, GLGetAttribLocationFunc) \
    X(glVertexAttribPointer, GLVertexAttribPointerFunc) \
    X(glEnableVertexAttribArray, GLEnableVertexAttribArrayFunc) \
    X(glDisableVertexAttribArray, GLDisableVertexAttribArrayFunc) \
    X(glGetUniformLocation, GLGetUniformLocationFunc) \
    X(glUniform1i, GLUniform1iFunc) \
    X(glUniform1f, GLUniform1fFunc) \
    X(glUniformMatrix4fv, GLUniformMatrix4fvFunc) \
    X(glGenTextures, GLGenTexturesFunc) \
    X(glBindTexture, GLBindTextureFunc) \
    X(glTexImage2D, GLTexImage2DFunc) \
    X(glTexSubImage2D, GLTexSubImage2DFunc) \
    X(glTexParameteri, GLTexParameteriFunc) \
    X(glDeleteTextures, GLDeleteTexturesFunc) \
    X(glDrawElements, GLDrawElementsFunc) \
    X(glDrawArrays, GLDrawArraysFunc) \
    X(glPixelStorei, GLPixelStoreiFunc) \
    X(glClearColor, GLClearColorFunc) \
    X(glClear, GLClearFunc) \
    X(glViewport, GLViewportFunc) \
    X(glActiveTexture, GLActiveTextureFunc) \
    X(glEnable, GLEnableFunc) \
    X(glDisable, GLDisableFunc) \
    X(glIsEnabled, GLIsEnabledFunc) \
    X(glGetError, GLGetErrorFunc) \
    X(glGetIntegerv, GLGetIntegerVFunc) \
    X(glBlendFunc, GLBlendFuncFunc) \
    X(glScissor, GLScissorFunc) \
    X(glUniform2f, GLUniform2fFunc) \
    X(glGenFramebuffers, GLGenFramebuffersFunc) \
    X(glDeleteFramebuffers, GLDeleteFramebuffersFunc) \
    X(glBindFramebuffer, GLBindFramebufferFunc) \
    X(glFramebufferTexture2D, GLFramebufferTexture2DFunc) \
    X(glCheckFramebufferStatus, GLCheckFramebufferStatusFunc)

using GLPixelStoreiFunc = void (APIENTRY*)(GLenum, GLint);
#define X(n, t) extern t n;
BROWSER_GL_FUNCS(X)
#undef X

} // namespace browser::platform
