// Minimal OpenGL 3.3 Core loader. All entry points are resolved through glfwGetProcAddress,
// so no system GL header and no generated loader are required.
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_LINE_SMOOTH 0x0B20
#define GL_MULTISAMPLE 0x809D
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VIEWPORT 0x0BA2
#define GL_FRAMEBUFFER_BINDING 0x8CA6

// X(return, name, params)
#define AE_GL_FUNCTIONS(X)                                                                        \
    X(void, glClear, (GLbitfield mask))                                                           \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                           \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                 \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                  \
    X(void, glEnable, (GLenum cap))                                                               \
    X(void, glDisable, (GLenum cap))                                                              \
    X(void, glDepthMask, (GLboolean flag))                                                        \
    X(void, glDepthFunc, (GLenum func))                                                           \
    X(void, glBlendFunc, (GLenum s, GLenum d))                                                    \
    X(void, glBlendFuncSeparate, (GLenum sc, GLenum dc, GLenum sa, GLenum da))                    \
    X(void, glCullFace, (GLenum mode))                                                            \
    X(void, glFrontFace, (GLenum mode))                                                           \
    X(void, glPolygonOffset, (GLfloat f, GLfloat u))                                              \
    X(void, glLineWidth, (GLfloat w))                                                             \
    X(void, glGetIntegerv, (GLenum pname, GLint * data))                                          \
    X(const GLubyte*, glGetString, (GLenum name))                                                 \
    X(GLenum, glGetError, (void))                                                                 \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                           \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void* p))  \
    X(void, glGenTextures, (GLsizei n, GLuint * t))                                               \
    X(void, glDeleteTextures, (GLsizei n, const GLuint* t))                                       \
    X(void, glBindTexture, (GLenum target, GLuint t))                                             \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                          \
    X(void, glTexImage2D, (GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, \
                           GLenum ty, const void* p))                                             \
    X(void, glGenerateMipmap, (GLenum target))                                                    \
    X(void, glActiveTexture, (GLenum tex))                                                        \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                              \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* idx))           \
    X(void, glGenBuffers, (GLsizei n, GLuint * b))                                                \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* b))                                        \
    X(void, glBindBuffer, (GLenum target, GLuint b))                                              \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))       \
    X(void, glBufferSubData, (GLenum target, GLintptr off, GLsizeiptr size, const void* data))    \
    X(void, glGenVertexArrays, (GLsizei n, GLuint * a))                                           \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* a))                                   \
    X(void, glBindVertexArray, (GLuint a))                                                        \
    X(void, glEnableVertexAttribArray, (GLuint i))                                                \
    X(void, glVertexAttribPointer, (GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st,         \
                                    const void* p))                                               \
    X(GLuint, glCreateShader, (GLenum type))                                                      \
    X(void, glShaderSource, (GLuint s, GLsizei c, const GLchar* const* str, const GLint* len))    \
    X(void, glCompileShader, (GLuint s))                                                          \
    X(void, glGetShaderiv, (GLuint s, GLenum p, GLint * v))                                       \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei m, GLsizei * l, GLchar * log))                 \
    X(void, glDeleteShader, (GLuint s))                                                           \
    X(GLuint, glCreateProgram, (void))                                                            \
    X(void, glAttachShader, (GLuint p, GLuint s))                                                 \
    X(void, glBindAttribLocation, (GLuint p, GLuint i, const GLchar* name))                       \
    X(void, glLinkProgram, (GLuint p))                                                            \
    X(void, glGetProgramiv, (GLuint p, GLenum n, GLint * v))                                      \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei m, GLsizei * l, GLchar * log))                \
    X(void, glDeleteProgram, (GLuint p))                                                          \
    X(void, glUseProgram, (GLuint p))                                                             \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar* name))                                \
    X(void, glUniform1i, (GLint l, GLint v))                                                      \
    X(void, glUniform1f, (GLint l, GLfloat v))                                                    \
    X(void, glUniform2f, (GLint l, GLfloat a, GLfloat b))                                         \
    X(void, glUniform3f, (GLint l, GLfloat a, GLfloat b, GLfloat c))                              \
    X(void, glUniform4f, (GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                   \
    X(void, glUniformMatrix4fv, (GLint l, GLsizei c, GLboolean t, const GLfloat* v))              \
    X(void, glGenFramebuffers, (GLsizei n, GLuint * f))                                           \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* f))                                   \
    X(void, glBindFramebuffer, (GLenum target, GLuint f))                                         \
    X(void, glFramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l))         \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                          \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint * r))                                          \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* r))                                  \
    X(void, glBindRenderbuffer, (GLenum target, GLuint r))                                        \
    X(void, glRenderbufferStorage, (GLenum t, GLenum f, GLsizei w, GLsizei h))                    \
    X(void, glFramebufferRenderbuffer, (GLenum t, GLenum a, GLenum rt, GLuint r))

#define AE_GL_DECLARE(ret, name, params)        \
    typedef ret(GLAPIENTRY* PFN_##name) params; \
    extern PFN_##name name;
AE_GL_FUNCTIONS(AE_GL_DECLARE)
#undef AE_GL_DECLARE

namespace gl {
// Resolves every entry point. Returns false and fills `missing` with the first unresolved name.
bool load(const char** missing);
}  // namespace gl
