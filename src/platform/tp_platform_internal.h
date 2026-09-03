/* tp_platform_internal.h — shared between tp_platform.c and the backends.
 * Not part of the public interface. */
#ifndef TP_PLATFORM_INTERNAL_H
#define TP_PLATFORM_INTERNAL_H

#include "tp_platform.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

typedef struct tp_platform tp_platform;

typedef struct {
    const char *name;
    tp_result (*init)(tp_platform *p, const tp_platform_desc *d);
    void      (*shutdown)(tp_platform *p);
    tp_result (*begin_frame)(tp_platform *p);
    tp_result (*end_frame)(tp_platform *p);
} tp_backend_vtbl;

struct tp_platform {
    tp_backend_vtbl vt;
    void           *bd;        /* backend-private state                    */
    tp_backend_kind kind;

    /* EGL, owned here because both backends set it up the same way apart
     * from the platform enum and native handle. */
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface surf;           /* EGL_NO_SURFACE for surfaceless           */
    EGLConfig  cfg;

    int  width, height;
    int  refresh_hz;
    int  msaa;
    bool vsync;
    bool srgb;

    /* Backend's target FBO. 0 means the EGL default framebuffer. */
    u32 fbo;

    tp_caps caps;
    u64     frame_index;
};

/* --- shared EGL helpers (tp_egl.c) --- */

/* Open an EGL display for `platform_enum` + `native`, initialise it, pick a
 * config and create a GLES 3.0 context. Fills p->dpy/cfg/ctx. Does not make
 * the context current and does not create a surface. */
tp_result tp_egl_setup(tp_platform *p, EGLenum platform_enum, void *native,
                       const tp_platform_desc *d, bool want_window_surface);
void tp_egl_teardown(tp_platform *p);
const char *tp_egl_error_str(EGLint err);
/* Log the last EGL error with context. Always returns TP_ERR_PLATFORM. */
tp_result tp_egl_fail(const char *what);
bool tp_egl_client_extension(const char *name);

/* --- backend constructors --- */
tp_result tp_backend_surfaceless_create(tp_platform *p, const tp_platform_desc *d);
#ifdef TP_HAVE_KMS
tp_result tp_backend_kms_create(tp_platform *p, const tp_platform_desc *d);
#endif

#endif /* TP_PLATFORM_INTERNAL_H */
