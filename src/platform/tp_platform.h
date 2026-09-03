/* tp_platform.h — backend-agnostic window/surface/context layer.
 *
 * The renderer above this line never mentions DRM, GBM or EGL. Two backends
 * implement it:
 *
 *   kms          /dev/dri/card0 -> GBM -> EGL -> drmModePageFlip. On-screen,
 *                vsync-locked, DRM master, no compositor. This is the real
 *                deployment path and the one PLAN.md §5 describes. It needs
 *                libdrm and libgbm at build time.
 *
 *   surfaceless  EGL_PLATFORM_SURFACELESS_MESA -> FBO. No display, no DRM
 *                master, no libdrm and no libgbm. Runs over SSH, runs in a
 *                terminal under X, and on the board it runs against
 *                /dev/dri/renderD128 alone — which CONTRACT.md §5 points out
 *                is much closer to certain than card0, since it needs nothing
 *                out-of-tree. This is how you get an on-device milestone
 *                before HDMI is proven.
 *
 * Both produce the same GL state to the layer above, so the scene code is
 * written once.
 */
#ifndef TP_PLATFORM_H
#define TP_PLATFORM_H

#include "../core/tp_common.h"
#include "../gfx/tp_caps.h"

typedef enum {
    TP_BACKEND_AUTO = 0,  /* kms if it can be opened, else surfaceless */
    TP_BACKEND_KMS,
    TP_BACKEND_SURFACELESS
} tp_backend_kind;

const char *tp_backend_str(tp_backend_kind k);
tp_backend_kind tp_backend_parse(const char *s);
/* Was the kms backend compiled in? False when libdrm/libgbm were absent. */
bool tp_backend_kms_available(void);

typedef struct {
    tp_backend_kind backend;
    const char *device;   /* NULL = auto ("/dev/dri/card0" for kms)         */
    int  width, height;   /* 0,0 = connector preferred mode (kms) or 1280x720 */
    int  refresh_hz;      /* 0 = don't care                                 */
    int  msaa;            /* 0, 2, 4. CONTRACT §2.2: 4x is cheap on a tiler */
    bool vsync;           /* kms: wait for the flip. surfaceless: no-op     */
    bool srgb;            /* request an sRGB-capable framebuffer            */
    tp_profile profile;   /* capability masking, see tp_caps.h              */
} tp_platform_desc;

void tp_platform_desc_defaults(tp_platform_desc *d);

typedef struct tp_platform tp_platform;

tp_result tp_platform_create(tp_platform **out, const tp_platform_desc *desc);
void      tp_platform_destroy(tp_platform *p);

/* Bind the backend's default framebuffer and set the viewport. */
tp_result tp_platform_begin_frame(tp_platform *p);
/* Present. On kms this resolves MSAA if needed, swaps, and page-flips,
 * blocking on the flip event when vsync is on. On surfaceless it flushes. */
tp_result tp_platform_end_frame(tp_platform *p);

void tp_platform_size(const tp_platform *p, int *w, int *h);
f32  tp_platform_aspect(const tp_platform *p);
const tp_caps  *tp_platform_caps(const tp_platform *p);
tp_backend_kind tp_platform_backend(const tp_platform *p);
/* Refresh rate of the active mode, or 0 when unknown. */
int  tp_platform_refresh_hz(const tp_platform *p);

/* Read the current framebuffer back as tightly packed RGB8, top row first.
 * Allocates; caller frees with tp_free. Used by --shot, which is the whole
 * on-device milestone when there is no display yet. */
tp_result tp_platform_read_rgb(tp_platform *p, u8 **out_rgb, int *w, int *h);

/* Write a PNG. Deliberately a minimal zlib-free encoder (stored deflate
 * blocks) so the binary links nothing extra — CONTRACT.md §1 lists exactly
 * four graphics libraries in the image and libpng is not among them. */
tp_result tp_platform_write_png(const char *path, const u8 *rgb, int w, int h);

#endif /* TP_PLATFORM_H */
