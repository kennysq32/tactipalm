/* tp_platform.c — backend selection, lifecycle, readback, PNG output. */
#define _POSIX_C_SOURCE 200809L
#include "tp_platform_internal.h"
#include "../gfx/tp_gl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Defined by the surfaceless backend; used by readback. */
GLuint tp_surfaceless_read_fbo(const tp_platform *p);

const char *tp_backend_str(tp_backend_kind k)
{
    switch (k) {
    case TP_BACKEND_AUTO:        return "auto";
    case TP_BACKEND_KMS:         return "kms";
    case TP_BACKEND_SURFACELESS: return "surfaceless";
    }
    return "?";
}

tp_backend_kind tp_backend_parse(const char *s)
{
    if (!s) return TP_BACKEND_AUTO;
    if (!strcmp(s, "auto")) return TP_BACKEND_AUTO;
    if (!strcmp(s, "kms") || !strcmp(s, "drm")) return TP_BACKEND_KMS;
    if (!strcmp(s, "surfaceless") || !strcmp(s, "headless") ||
        !strcmp(s, "offscreen"))
        return TP_BACKEND_SURFACELESS;
    TP_WARN("unknown backend '%s', using auto", s);
    return TP_BACKEND_AUTO;
}

bool tp_backend_kms_available(void)
{
#ifdef TP_HAVE_KMS
    return true;
#else
    return false;
#endif
}

void tp_platform_desc_defaults(tp_platform_desc *d)
{
    memset(d, 0, sizeof(*d));
    d->backend    = TP_BACKEND_AUTO;
    d->device     = NULL;
    d->width      = 1280;   /* PLAN.md §1: 720p is the baseline, 1080p a stretch */
    d->height     = 720;
    d->refresh_hz = 0;
    d->msaa       = 4;      /* CONTRACT.md §2.2: cheap on a tiler, on by default */
    d->vsync      = true;
    d->srgb       = false;
    d->profile    = TP_PROFILE_MALI_G31;
}

/* Can we plausibly take the KMS path? Cheap enough to just try opening it. */
static bool kms_device_usable(const char *dev)
{
    const char *path = dev ? dev : "/dev/dri/card0";
    if (access(path, R_OK | W_OK) != 0) {
        TP_DEBUG("auto: %s not accessible, skipping kms", path);
        return false;
    }
    return true;
}

tp_result tp_platform_create(tp_platform **out, const tp_platform_desc *desc)
{
    *out = NULL;

    tp_platform_desc d;
    if (desc) d = *desc; else tp_platform_desc_defaults(&d);

    tp_platform *p = (tp_platform *)tp_alloc(sizeof(*p));
    if (!p) return TP_ERR_NOMEM;

    p->dpy   = EGL_NO_DISPLAY;
    p->ctx   = EGL_NO_CONTEXT;
    p->surf  = EGL_NO_SURFACE;
    p->msaa  = d.msaa;
    p->vsync = d.vsync;

    tp_backend_kind want = d.backend;
    if (want == TP_BACKEND_AUTO) {
        want = (tp_backend_kms_available() && kms_device_usable(d.device))
             ? TP_BACKEND_KMS : TP_BACKEND_SURFACELESS;
        TP_DEBUG("auto backend selection -> %s", tp_backend_str(want));
    }

    if (want == TP_BACKEND_KMS && !tp_backend_kms_available()) {
        TP_WARN("kms backend requested but not compiled in "
                "(libdrm/libgbm headers were missing at build time); "
                "falling back to surfaceless");
        want = TP_BACKEND_SURFACELESS;
    }

    tp_result r;
    switch (want) {
#ifdef TP_HAVE_KMS
    case TP_BACKEND_KMS:
        r = tp_backend_kms_create(p, &d);
        /* A desktop session already holds DRM master on card0, so this is the
         * expected failure when you forget to switch to a VT. Falling back
         * beats dying, as long as we say why. */
        if (r != TP_OK && d.backend == TP_BACKEND_AUTO) {
            TP_WARN("kms backend failed (%s); falling back to surfaceless. "
                    "For on-screen output run from a real VT with no "
                    "compositor holding DRM master.", tp_result_str(r));
            if (p->vt.shutdown) p->vt.shutdown(p);
            tp_egl_teardown(p);
            memset(&p->vt, 0, sizeof(p->vt));
            r = tp_backend_surfaceless_create(p, &d);
        }
        break;
#endif
    case TP_BACKEND_SURFACELESS:
    default:
        r = tp_backend_surfaceless_create(p, &d);
        break;
    }

    if (r != TP_OK) {
        tp_platform_destroy(p);
        return r;
    }

    tp_caps_log(&p->caps);

    r = tp_caps_require_baseline(&p->caps);
    if (r != TP_OK) {
        tp_platform_destroy(p);
        return r;
    }

    *out = p;
    return TP_OK;
}

void tp_platform_destroy(tp_platform *p)
{
    if (!p) return;
    if (p->vt.shutdown) p->vt.shutdown(p);
    tp_caps_free(&p->caps);
    tp_egl_teardown(p);
    tp_free(p);
}

tp_result tp_platform_begin_frame(tp_platform *p)
{
    return p->vt.begin_frame ? p->vt.begin_frame(p) : TP_OK;
}

tp_result tp_platform_end_frame(tp_platform *p)
{
    tp_result r = p->vt.end_frame ? p->vt.end_frame(p) : TP_OK;
    p->frame_index++;
    return r;
}

void tp_platform_size(const tp_platform *p, int *w, int *h)
{
    if (w) *w = p->width;
    if (h) *h = p->height;
}

f32 tp_platform_aspect(const tp_platform *p)
{
    return p->height > 0 ? (f32)p->width / (f32)p->height : 1.0f;
}

const tp_caps *tp_platform_caps(const tp_platform *p) { return &p->caps; }
tp_backend_kind tp_platform_backend(const tp_platform *p) { return p->kind; }
int tp_platform_refresh_hz(const tp_platform *p) { return p->refresh_hz; }

/* ---- readback -------------------------------------------------------- */

tp_result tp_platform_read_rgb(tp_platform *p, u8 **out_rgb, int *w, int *h)
{
    *out_rgb = NULL;

    const int W = p->width, H = p->height;
    /* GLES 3.0 guarantees exactly one always-supported combination for
     * glReadPixels — GL_RGBA/GL_UNSIGNED_BYTE — so read RGBA and drop the
     * alpha ourselves rather than trusting GL_RGB to be accepted. */
    size_t rgba_bytes = (size_t)W * (size_t)H * 4;
    u8 *rgba = (u8 *)tp_alloc(rgba_bytes);
    if (!rgba) return TP_ERR_NOMEM;

    GLuint read_fbo = p->fbo;
    if (p->kind == TP_BACKEND_SURFACELESS)
        read_fbo = tp_surfaceless_read_fbo(p);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    if (!tp_gl_check("glReadPixels")) {
        tp_free(rgba);
        return TP_ERR_GL;
    }

    size_t rgb_bytes = (size_t)W * (size_t)H * 3;
    u8 *rgb = (u8 *)tp_alloc(rgb_bytes);
    if (!rgb) { tp_free(rgba); return TP_ERR_NOMEM; }

    /* GL origin is bottom-left, image formats are top-left: flip while we
     * are already touching every pixel. */
    for (int y = 0; y < H; ++y) {
        const u8 *src = rgba + (size_t)(H - 1 - y) * (size_t)W * 4;
        u8 *dst = rgb + (size_t)y * (size_t)W * 3;
        for (int x = 0; x < W; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    tp_free(rgba);

    *out_rgb = rgb;
    if (w) *w = W;
    if (h) *h = H;
    return TP_OK;
}

/* ---- PNG ------------------------------------------------------------- */
/* A deliberately tiny encoder. CONTRACT.md §1 lists mesa, libdrm, libgbm and
 * libglvnd as the only graphics libraries in the image, and adding libpng to
 * the package set to write a debug screenshot is a bad trade. Deflate has a
 * "stored" block type that is literally length-prefixed raw bytes, so a valid
 * zlib stream can be produced with no compressor at all. The file is about
 * as large as the raw pixels; it is a diagnostic, not an asset. */

static u32 crc32_of(const u8 *buf, size_t len, u32 crc)
{
    static u32 table[256];
    static bool built = false;
    if (!built) {
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[n] = c;
        }
        built = true;
    }
    crc = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);  p[3] = (u8)v;
}

static bool write_chunk(FILE *f, const char *type, const u8 *data, size_t len)
{
    u8 hdr[8];
    put_be32(hdr, (u32)len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return false;
    if (len && fwrite(data, 1, len, f) != len) return false;

    u32 crc = crc32_of((const u8 *)type, 4, 0);
    if (len) crc = crc32_of(data, len, crc);
    u8 crcb[4];
    put_be32(crcb, crc);
    return fwrite(crcb, 1, 4, f) == 4;
}

tp_result tp_platform_write_png(const char *path, const u8 *rgb, int w, int h)
{
    if (w <= 0 || h <= 0 || !rgb) return TP_ERR_ARGS;

    FILE *f = fopen(path, "wb");
    if (!f) return TP_FAIL(TP_ERR_IO, "cannot write '%s'", path);

    static const u8 sig[8] = {0x89,'P','N','G','\r','\n',0x1A,'\n'};
    if (fwrite(sig, 1, 8, f) != 8) { fclose(f); return TP_ERR_IO; }

    u8 ihdr[13];
    put_be32(ihdr + 0, (u32)w);
    put_be32(ihdr + 4, (u32)h);
    ihdr[8]  = 8;  /* bit depth   */
    ihdr[9]  = 2;  /* colour type 2 = truecolour RGB */
    ihdr[10] = 0;  /* deflate     */
    ihdr[11] = 0;  /* filter      */
    ihdr[12] = 0;  /* no interlace*/
    if (!write_chunk(f, "IHDR", ihdr, sizeof(ihdr))) { fclose(f); return TP_ERR_IO; }

    /* Raw scanlines, each prefixed with filter byte 0. */
    const size_t stride = (size_t)w * 3;
    const size_t raw_len = (stride + 1) * (size_t)h;
    u8 *raw = (u8 *)tp_alloc(raw_len);
    if (!raw) { fclose(f); return TP_ERR_NOMEM; }
    for (int y = 0; y < h; ++y) {
        raw[(stride + 1) * (size_t)y] = 0;
        memcpy(raw + (stride + 1) * (size_t)y + 1, rgb + stride * (size_t)y, stride);
    }

    /* zlib wrapper + stored deflate blocks, max 65535 payload bytes each. */
    size_t nblocks = (raw_len + 65534) / 65535;
    if (nblocks == 0) nblocks = 1;
    size_t z_len = 2 + raw_len + nblocks * 5 + 4;
    u8 *z = (u8 *)tp_alloc(z_len);
    if (!z) { tp_free(raw); fclose(f); return TP_ERR_NOMEM; }

    size_t zi = 0;
    z[zi++] = 0x78; /* CMF: deflate, 32K window */
    z[zi++] = 0x01; /* FLG: no dict, check bits make (0x78<<8|0x01) % 31 == 0 */

    size_t off = 0;
    while (off < raw_len) {
        size_t n = raw_len - off;
        if (n > 65535) n = 65535;
        bool final = (off + n >= raw_len);
        z[zi++] = final ? 1 : 0;
        z[zi++] = (u8)(n & 0xFF);
        z[zi++] = (u8)(n >> 8);
        z[zi++] = (u8)(~n & 0xFF);
        z[zi++] = (u8)((~n >> 8) & 0xFF);
        memcpy(z + zi, raw + off, n);
        zi += n;
        off += n;
    }

    /* Adler-32 over the uncompressed data. */
    u32 a = 1, b = 0;
    for (size_t i = 0; i < raw_len; ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(z + zi, (b << 16) | a);
    zi += 4;

    bool ok = write_chunk(f, "IDAT", z, zi) && write_chunk(f, "IEND", NULL, 0);

    tp_free(z);
    tp_free(raw);
    fclose(f);

    if (!ok) return TP_FAIL(TP_ERR_IO, "short write to '%s'", path);
    TP_INFO("wrote %s (%dx%d, %zu KiB)", path, w, h, (zi + 512) / 1024);
    return TP_OK;
}
