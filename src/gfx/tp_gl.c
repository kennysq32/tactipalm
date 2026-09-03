/* tp_gl.c — GL error plumbing. */
#include "tp_gl.h"

const char *tp_gl_enum_str(GLenum e)
{
    switch (e) {
    case GL_NO_ERROR:                      return "GL_NO_ERROR";
    case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
    default:                               return "GL_<unknown>";
    }
}

bool tp_gl_check(const char *where)
{
    bool clean = true;
    GLenum e;
    /* Drain: the queue can hold several, and leaving any behind makes the
     * next check blame the wrong call site. */
    int guard = 0;
    while ((e = glGetError()) != GL_NO_ERROR && guard++ < 32) {
        TP_ERROR("GL error at %s: %s (0x%04x)", where, tp_gl_enum_str(e), (unsigned)e);
        clean = false;
    }
    return clean;
}
