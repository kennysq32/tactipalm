/* tp_surfaceless.c — offscreen backend: EGL surfaceless -> FBO.
 *
 * No DRM master, no GBM, no display, no libdrm and no libgbm link. It renders
 * into a framebuffer object and can hand the pixels back.
 *
 * Two reasons this backend exists and is not merely a testing convenience:
 *
 *  1. On the dev box it runs inside a normal desktop session, so the whole
 *     renderer is developed and debugged without dropping to a VT and taking
 *     DRM master away from the compositor.
 *
 *  2. On the Orange Pi it is the milestone available *today*. CONTRACT.md §5:
 *     card0 depends on 44 out-of-tree DE33 patches and is "a promise, not a
 *     fact", while renderD128 is Panfrost, fully upstream, and needs nothing
 *     out of tree. Rendering offscreen and writing a PNG proves the GPU, the
 *     driver, the shaders and the whole scene on real hardware before HDMI
 *     lights up at all.
 */
#include "tp_platform_internal.h"
#include "../gfx/tp_gl.h"

#include <string.h>

typedef struct {
    GLuint fbo_msaa;      /* 0 when msaa is off                            */
    GLuint rbo_color_ms;
    GLuint rbo_depth_ms;

    GLuint fbo_resolve;   /* always present; the readback source            */
    GLuint rbo_color;
    GLuint rbo_depth;     /* only used when msaa is off                     */

    bool   used_pbuffer;  /* fallback when surfaceless contexts are absent  */
} tp_surfaceless;

static const char *fb_status_str(GLenum s)
{
    switch (s) {
    case GL_FRAMEBUFFER_COMPLETE: return "complete";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "incomplete attachment";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "missing attachment";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "incomplete multisample";
    case GL_FRAMEBUFFER_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}

static tp_result build_targets(tp_platform *p, tp_surfaceless *s)
{
    const GLsizei w = (GLsizei)p->width, h = (GLsizei)p->height;

    /* Resolve target: single-sample colour, plus depth when there is no MSAA
     * pass in front of it. GL_RGBA8 rather than GL_RGB8 because RGB8 is not a
     * required colour-renderable format in GLES 3.0 and Panfrost is exactly
     * the kind of driver that will hold you to that. */
    glGenFramebuffers(1, &s->fbo_resolve);
    glGenRenderbuffers(1, &s->rbo_color);
    glBindRenderbuffer(GL_RENDERBUFFER, s->rbo_color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_resolve);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, s->rbo_color);

    if (p->msaa <= 1) {
        glGenRenderbuffers(1, &s->rbo_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, s->rbo_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, s->rbo_depth);
    }

    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE)
        return TP_FAIL(TP_ERR_GL, "resolve framebuffer %s (0x%04x)",
                       fb_status_str(st), (unsigned)st);

    if (p->msaa > 1) {
        glGenFramebuffers(1, &s->fbo_msaa);
        glGenRenderbuffers(1, &s->rbo_color_ms);
        glGenRenderbuffers(1, &s->rbo_depth_ms);

        glBindRenderbuffer(GL_RENDERBUFFER, s->rbo_color_ms);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, p->msaa, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, s->rbo_depth_ms);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, p->msaa,
                                         GL_DEPTH_COMPONENT24, w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, s->fbo_msaa);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, s->rbo_color_ms);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, s->rbo_depth_ms);

        st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE)
            return TP_FAIL(TP_ERR_GL, "%dx MSAA framebuffer %s (0x%04x)",
                           p->msaa, fb_status_str(st), (unsigned)st);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    p->fbo = s->fbo_msaa ? s->fbo_msaa : s->fbo_resolve;
    return tp_gl_check("surfaceless target creation") ? TP_OK
         : TP_FAIL(TP_ERR_GL, "GL errors while building offscreen targets");
}

static tp_result sl_init(tp_platform *p, const tp_platform_desc *d)
{
    tp_surfaceless *s = (tp_surfaceless *)tp_alloc(sizeof(*s));
    if (!s) return TP_ERR_NOMEM;
    p->bd = s;

    if (!tp_egl_client_extension("EGL_MESA_platform_surfaceless")) {
        /* Not fatal: eglGetPlatformDisplay may still accept the enum. Worth a
         * warning because when it does fail, the error is opaque. */
        TP_WARN("EGL_MESA_platform_surfaceless not advertised; trying anyway");
    }

    tp_result r = tp_egl_setup(p, EGL_PLATFORM_SURFACELESS_MESA,
                               EGL_DEFAULT_DISPLAY, d, false);
    if (r != TP_OK) return r;

    p->width  = d->width  > 0 ? d->width  : 1280;
    p->height = d->height > 0 ? d->height : 720;
    p->refresh_hz = 0; /* no display, no refresh rate to speak of */

    const char *dpy_exts = eglQueryString(p->dpy, EGL_EXTENSIONS);
    bool have_surfaceless_ctx =
        dpy_exts && strstr(dpy_exts, "EGL_KHR_surfaceless_context");

    if (have_surfaceless_ctx) {
        p->surf = EGL_NO_SURFACE;
        if (!eglMakeCurrent(p->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, p->ctx))
            return tp_egl_fail("eglMakeCurrent(surfaceless)");
    } else {
        /* A pbuffer costs a redundant allocation we never draw into, but it
         * keeps the backend working on drivers without surfaceless contexts. */
        TP_WARN("no EGL_KHR_surfaceless_context; falling back to a 1x1 pbuffer");
        const EGLint pb[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        p->surf = eglCreatePbufferSurface(p->dpy, p->cfg, pb);
        if (p->surf == EGL_NO_SURFACE) return tp_egl_fail("eglCreatePbufferSurface");
        if (!eglMakeCurrent(p->dpy, p->surf, p->surf, p->ctx))
            return tp_egl_fail("eglMakeCurrent(pbuffer)");
        s->used_pbuffer = true;
    }

    /* Caps must be queried with a context current, and before any target is
     * built, so MAX_SAMPLES can veto the requested MSAA level. */
    r = tp_caps_query(&p->caps, d->profile);
    if (r != TP_OK) return r;

    p->msaa = d->msaa;
    if (p->msaa > 1 && p->msaa > p->caps.max_samples) {
        TP_WARN("%dx MSAA requested, driver/profile caps at %dx",
                p->msaa, p->caps.max_samples);
        p->msaa = p->caps.max_samples;
    }
    if (p->msaa == 1) p->msaa = 0;

    r = build_targets(p, s);
    if (r != TP_OK) return r;

    TP_INFO("surfaceless backend: %dx%d, %s, offscreen FBO %u",
            p->width, p->height,
            p->msaa > 1 ? "MSAA" : "no MSAA", (unsigned)p->fbo);
    if (p->msaa > 1) TP_INFO("                     %dx MSAA -> resolve blit", p->msaa);
    return TP_OK;
}

static void sl_shutdown(tp_platform *p)
{
    tp_surfaceless *s = (tp_surfaceless *)p->bd;
    if (!s) return;

    /* Only touch GL if the context is still current — shutdown runs on the
     * error path too, where it may never have been made current at all. */
    if (p->dpy != EGL_NO_DISPLAY && eglGetCurrentContext() == p->ctx) {
        if (s->fbo_msaa)    glDeleteFramebuffers(1, &s->fbo_msaa);
        if (s->fbo_resolve) glDeleteFramebuffers(1, &s->fbo_resolve);
        GLuint rbos[4]; int n = 0;
        if (s->rbo_color_ms) rbos[n++] = s->rbo_color_ms;
        if (s->rbo_depth_ms) rbos[n++] = s->rbo_depth_ms;
        if (s->rbo_color)    rbos[n++] = s->rbo_color;
        if (s->rbo_depth)    rbos[n++] = s->rbo_depth;
        if (n) glDeleteRenderbuffers(n, rbos);
    }

    tp_free(s);
    p->bd = NULL;
}

static tp_result sl_begin_frame(tp_platform *p)
{
    glBindFramebuffer(GL_FRAMEBUFFER, p->fbo);
    glViewport(0, 0, p->width, p->height);
    return TP_OK;
}

static tp_result sl_end_frame(tp_platform *p)
{
    tp_surfaceless *s = (tp_surfaceless *)p->bd;

    if (s->fbo_msaa) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbo_msaa);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->fbo_resolve);
        glBlitFramebuffer(0, 0, p->width, p->height,
                          0, 0, p->width, p->height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* There is no page flip to pace us here, so without this the loop races
     * ahead of the GPU and every frame-time measurement is a lie. */
    glFinish();
    return TP_OK;
}

tp_result tp_backend_surfaceless_create(tp_platform *p, const tp_platform_desc *d)
{
    p->vt = (tp_backend_vtbl){
        .name        = "surfaceless",
        .init        = sl_init,
        .shutdown    = sl_shutdown,
        .begin_frame = sl_begin_frame,
        .end_frame   = sl_end_frame
    };
    p->kind = TP_BACKEND_SURFACELESS;
    return p->vt.init(p, d);
}

/* Readback source: after end_frame the resolve FBO holds the finished image
 * whether or not MSAA was used. */
GLuint tp_surfaceless_read_fbo(const tp_platform *p)
{
    const tp_surfaceless *s = (const tp_surfaceless *)p->bd;
    return s ? s->fbo_resolve : 0;
}
