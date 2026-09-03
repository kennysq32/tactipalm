/* tp_egl.c — EGL display/config/context setup shared by every backend.
 *
 * The one policy decision here is the context version: we ask for exactly
 * GLES 3.0. CONTRACT.md §2 says Panfrost advertises 3.1 on Mali-G31 and that
 * compute and SSBOs are where its bugs concentrate, so 3.0 is the target.
 * EGL is allowed to hand back a higher backwards-compatible version and Mesa
 * usually does; the real enforcement is `#version 300 es` in every shader
 * plus the capability profile in tp_caps.c. Asking for 3.0 anyway keeps the
 * intent in the code and makes a driver that *can* honour it do so.
 */
#include "tp_platform_internal.h"

#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>

const char *tp_egl_error_str(EGLint err)
{
    switch (err) {
    case EGL_SUCCESS:             return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
    default:                      return "EGL_<unknown>";
    }
}

tp_result tp_egl_fail(const char *what)
{
    EGLint e = eglGetError();
    return TP_FAIL(TP_ERR_PLATFORM, "%s failed: %s (0x%04x)",
                   what, tp_egl_error_str(e), (unsigned)e);
}

bool tp_egl_client_extension(const char *name)
{
    /* EGL_EXT_client_extensions lets us query before any display exists. A
     * driver that does not support it returns NULL, which we treat as "no". */
    const char *exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (!exts) return false;

    size_t nlen = strlen(name);
    const char *p = exts;
    while ((p = strstr(p, name)) != NULL) {
        bool l = (p == exts) || p[-1] == ' ';
        bool r = (p[nlen] == ' ' || p[nlen] == '\0');
        if (l && r) return true;
        p += nlen;
    }
    return false;
}

static void log_config(EGLDisplay dpy, EGLConfig cfg)
{
    EGLint r = 0, g = 0, b = 0, a = 0, d = 0, s = 0, samples = 0, id = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &id);
    eglGetConfigAttrib(dpy, cfg, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(dpy, cfg, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(dpy, cfg, EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(dpy, cfg, EGL_DEPTH_SIZE, &d);
    eglGetConfigAttrib(dpy, cfg, EGL_STENCIL_SIZE, &s);
    eglGetConfigAttrib(dpy, cfg, EGL_SAMPLES, &samples);
    TP_DEBUG("EGL config #%d: RGBA %d%d%d%d  depth %d  stencil %d  samples %d",
             id, r, g, b, a, d, s, samples);
}

tp_result tp_egl_setup(tp_platform *p, EGLenum platform_enum, void *native,
                       const tp_platform_desc *d, bool want_window_surface)
{
    /* eglGetPlatformDisplay is EGL 1.5 core; the EXT form covers 1.4 drivers.
     * Panfrost/Mesa has both, but the fallback costs three lines. */
    p->dpy = EGL_NO_DISPLAY;

    if (tp_egl_client_extension("EGL_EXT_platform_base")) {
        PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy_ext =
            (PFNEGLGETPLATFORMDISPLAYEXTPROC)
                eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (get_dpy_ext)
            p->dpy = get_dpy_ext(platform_enum, native, NULL);
    }
    if (p->dpy == EGL_NO_DISPLAY)
        p->dpy = eglGetPlatformDisplay(platform_enum, native, NULL);

    if (p->dpy == EGL_NO_DISPLAY)
        return tp_egl_fail("eglGetPlatformDisplay");

    EGLint major = 0, minor = 0;
    if (!eglInitialize(p->dpy, &major, &minor)) {
        p->dpy = EGL_NO_DISPLAY;
        return tp_egl_fail("eglInitialize");
    }
    TP_INFO("EGL %d.%d — %s", major, minor,
            eglQueryString(p->dpy, EGL_VENDOR));
    TP_DEBUG("EGL display extensions: %s",
             eglQueryString(p->dpy, EGL_EXTENSIONS));

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        tp_egl_teardown(p);
        return tp_egl_fail("eglBindAPI(EGL_OPENGL_ES_API)");
    }

    /* Config. XRGB8888 scanout (CONTRACT.md §2) means 8/8/8 with no alpha
     * required; asking for EGL_ALPHA_SIZE 0 is wrong on some drivers because
     * it is a minimum, not an exact match, so we sort it out after the fact
     * on the kms path by matching the GBM format instead. */
    EGLint want_surface_bit = want_window_surface ? EGL_WINDOW_BIT
                                                  : EGL_PBUFFER_BIT;
    EGLint attrs[] = {
        EGL_SURFACE_TYPE,    want_surface_bit,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    0,
        /* MSAA on a tiler resolves in tile memory, so 4x is cheap on the
         * target and worth having on by default (CONTRACT.md §2.2). */
        EGL_SAMPLES,         d->msaa > 1 ? d->msaa : 0,
        EGL_NONE
    };

    EGLConfig configs[64];
    EGLint n = 0;
    if (!eglChooseConfig(p->dpy, attrs, configs, TP_ARRAY_LEN(configs), &n) || n == 0) {
        if (d->msaa > 1) {
            TP_WARN("no EGL config with %dx MSAA; retrying without", d->msaa);
            for (int i = 0; attrs[i] != EGL_NONE; i += 2)
                if (attrs[i] == EGL_SAMPLES) attrs[i + 1] = 0;
            p->msaa = 0;
            if (!eglChooseConfig(p->dpy, attrs, configs, TP_ARRAY_LEN(configs), &n) || n == 0) {
                tp_egl_teardown(p);
                return tp_egl_fail("eglChooseConfig");
            }
        } else {
            tp_egl_teardown(p);
            return tp_egl_fail("eglChooseConfig");
        }
    }

    /* eglChooseConfig's sort order prefers larger buffers, which on a bandwidth
     * bound tiler is exactly backwards — take the first config with no more
     * than 8 bits of alpha rather than whatever it ranked first. */
    p->cfg = configs[0];
    for (EGLint i = 0; i < n; ++i) {
        EGLint a = 0;
        eglGetConfigAttrib(p->dpy, configs[i], EGL_ALPHA_SIZE, &a);
        if (a == 0) { p->cfg = configs[i]; break; }
    }
    log_config(p->dpy, p->cfg);

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    p->ctx = eglCreateContext(p->dpy, p->cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (p->ctx == EGL_NO_CONTEXT) {
        tp_egl_teardown(p);
        return tp_egl_fail("eglCreateContext(GLES 3.0)");
    }

    p->srgb = d->srgb;
    return TP_OK;
}

void tp_egl_teardown(tp_platform *p)
{
    if (p->dpy == EGL_NO_DISPLAY) return;

    eglMakeCurrent(p->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (p->surf != EGL_NO_SURFACE) {
        eglDestroySurface(p->dpy, p->surf);
        p->surf = EGL_NO_SURFACE;
    }
    if (p->ctx != EGL_NO_CONTEXT) {
        eglDestroyContext(p->dpy, p->ctx);
        p->ctx = EGL_NO_CONTEXT;
    }
    eglTerminate(p->dpy);
    p->dpy = EGL_NO_DISPLAY;
    /* Mesa leaks a thread-local without this; harmless elsewhere. */
    eglReleaseThread();
}
