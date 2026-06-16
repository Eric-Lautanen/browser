#pragma once
#include "../tests/utility.hpp"
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
using GLBlendFuncFunc = void (APIENTRY*)(GLenum, GLenum);
using GLScissorFunc = void (APIENTRY*)(GLint, GLint, GLsizei, GLsizei);

// Vertex buffer objects
extern GLGenBuffersFunc glGenBuffers;
extern GLBindBufferFunc glBindBuffer;
extern GLBufferDataFunc glBufferData;
extern GLBufferSubDataFunc glBufferSubData;
extern GLDeleteBuffersFunc glDeleteBuffers;

// Vertex array objects
extern GLGenVertexArraysFunc glGenVertexArrays;
extern GLBindVertexArrayFunc glBindVertexArray;
extern GLDeleteVertexArraysFunc glDeleteVertexArrays;

// Shader objects
extern GLCreateShaderFunc glCreateShader;
extern GLShaderSourceFunc glShaderSource;
extern GLCompileShaderFunc glCompileShader;
extern GLGetShaderivFunc glGetShaderiv;
extern GLGetShaderInfoLogFunc glGetShaderInfoLog;
extern GLDeleteShaderFunc glDeleteShader;
extern GLAttachShaderFunc glAttachShader;
extern GLLinkProgramFunc glLinkProgram;
extern GLUseProgramFunc glUseProgram;
extern GLCreateProgramFunc glCreateProgram;
extern GLDeleteProgramFunc glDeleteProgram;
extern GLGetProgramivFunc glGetProgramiv;
extern GLGetProgramInfoLogFunc glGetProgramInfoLog;

// Vertex attributes
extern GLGetAttribLocationFunc glGetAttribLocation;
extern GLVertexAttribPointerFunc glVertexAttribPointer;
extern GLEnableVertexAttribArrayFunc glEnableVertexAttribArray;
extern GLDisableVertexAttribArrayFunc glDisableVertexAttribArray;

// Uniforms
extern GLGetUniformLocationFunc glGetUniformLocation;
extern GLUniform1iFunc glUniform1i;
extern GLUniform1fFunc glUniform1f;
extern GLUniformMatrix4fvFunc glUniformMatrix4fv;

// Textures
extern GLGenTexturesFunc glGenTextures;
extern GLBindTextureFunc glBindTexture;
extern GLTexImage2DFunc glTexImage2D;
extern GLTexSubImage2DFunc glTexSubImage2D;
extern GLTexParameteriFunc glTexParameteri;
extern GLDeleteTexturesFunc glDeleteTextures;

// Drawing
extern GLDrawElementsFunc glDrawElements;
extern GLDrawArraysFunc glDrawArrays;

// Pixel store
using GLPixelStoreiFunc = void (APIENTRY*)(GLenum, GLint);
extern GLPixelStoreiFunc glPixelStorei;

// Clear / state
extern GLClearColorFunc glClearColor;
extern GLClearFunc glClear;
extern GLViewportFunc glViewport;

// Texture state
extern GLActiveTextureFunc glActiveTexture;

// Blending / depth
extern GLEnableFunc glEnable;
extern GLDisableFunc glDisable;
extern GLBlendFuncFunc glBlendFunc;
extern GLScissorFunc glScissor;

} // namespace browser::platform
