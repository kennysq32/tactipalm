/* tp_texture.c — texture objects with target-format enforcement. */
#include "tp_texture.h"
#include "tp_gl.h"

#include <stdio.h>
#include <string.h>

static GLenum wrap_enum(tp_wrap w)
{
    switch (w) {
    case TP_WRAP_CLAMP:  return GL_CLAMP_TO_EDGE;
    case TP_WRAP_MIRROR: return GL_MIRRORED_REPEAT;
    case TP_WRAP_REPEAT:
    default:             return GL_REPEAT;
    }
}

static void apply_sampler_state(tp_filter filter, tp_wrap wrap, bool have_mips)
{
    GLenum min_f, mag_f;
    switch (filter) {
    case TP_FILTER_NEAREST:
        min_f = have_mips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
        mag_f = GL_NEAREST;
        break;
    case TP_FILTER_TRILINEAR:
        min_f = have_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
        mag_f = GL_LINEAR;
        break;
    case TP_FILTER_LINEAR:
    default:
        min_f = have_mips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
        mag_f = GL_LINEAR;
        break;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)min_f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)mag_f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap_enum(wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap_enum(wrap));
}

static tp_result check_size(const tp_caps *caps, const char *name, int w, int h)
{
    if (w <= 0 || h <= 0)
        return TP_FAIL(TP_ERR_ARGS, "texture '%s': bad size %dx%d", name, w, h);
    if (caps && (w > caps->max_texture_size || h > caps->max_texture_size))
        return TP_FAIL(TP_ERR_CAPS,
                       "texture '%s' is %dx%d but the target caps out at %d — "
                       "downscale it in the asset pipeline, not at runtime",
                       name, w, h, caps->max_texture_size);
    return TP_OK;
}

tp_result tp_texture_create_2d(tp_texture *t, const tp_caps *caps,
                               const char *name,
                               int w, int h, u32 internal_format,
                               u32 format, u32 type, const void *pixels,
                               tp_filter filter, tp_wrap wrap)
{
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "texture");

    tp_result r = check_size(caps, t->name, w, h);
    if (r != TP_OK) return r;

    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    /* Rows are tightly packed everywhere in this renderer; the default of 4
     * silently corrupts any texture whose row length is not a multiple of 4. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, w, h, 0,
                 format, type, pixels);

    bool want_mips = (filter == TP_FILTER_TRILINEAR);
    if (want_mips) {
        glGenerateMipmap(GL_TEXTURE_2D);
        t->has_mipmaps = true;
    }
    apply_sampler_state(filter, wrap, t->has_mipmaps);
    glBindTexture(GL_TEXTURE_2D, 0);

    t->width = w;
    t->height = h;
    t->internal_format = internal_format;
    t->family = TP_TEXFMT_UNCOMPRESSED;

    if (!tp_gl_check("tp_texture_create_2d")) {
        tp_texture_destroy(t);
        return TP_ERR_GL;
    }
    TP_DEBUG("texture '%s': %dx%d uncompressed%s",
             t->name, w, h, t->has_mipmaps ? " +mips" : "");
    return TP_OK;
}

size_t tp_texture_compressed_size(u32 fmt, int w, int h)
{
    tp_texfmt_family fam = tp_texfmt_family_of(fmt);

    /* ETC2/EAC: 4x4 blocks, 8 bytes for RGB and punchthrough, 16 for RGBA. */
    if (fam == TP_TEXFMT_ETC2) {
        int bx = (w + 3) / 4, by = (h + 3) / 4;
        size_t block_bytes;
        switch (fmt) {
        case GL_COMPRESSED_RGBA8_ETC2_EAC:
        case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
        case GL_COMPRESSED_RG11_EAC:
        case GL_COMPRESSED_SIGNED_RG11_EAC:
            block_bytes = 16; break;
        default:
            block_bytes = 8; break;
        }
        return (size_t)bx * (size_t)by * block_bytes;
    }

    /* ASTC: always 16 bytes per block, block footprint varies. The KHR enums
     * are ordered 4x4, 5x4, 5x5, 6x5, 6x6, 8x5, 8x6, 8x8, 10x5, 10x6, 10x8,
     * 10x10, 12x10, 12x12 in both the linear and sRGB ranges. */
    if (fam == TP_TEXFMT_ASTC) {
        static const int dims[14][2] = {
            {4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},
            {8,8},{10,5},{10,6},{10,8},{10,10},{12,10},{12,12}
        };
        int idx = -1;
        if (fmt >= GL_COMPRESSED_RGBA_ASTC_4x4_KHR &&
            fmt <= GL_COMPRESSED_RGBA_ASTC_12x12_KHR)
            idx = (int)(fmt - GL_COMPRESSED_RGBA_ASTC_4x4_KHR);
        else if (fmt >= GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR &&
                 fmt <= GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR)
            idx = (int)(fmt - GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR);
        if (idx < 0 || idx >= 14) return 0;

        int bw = dims[idx][0], bh = dims[idx][1];
        return (size_t)((w + bw - 1) / bw) * (size_t)((h + bh - 1) / bh) * 16u;
    }

    return 0;
}

tp_result tp_texture_create_compressed(tp_texture *t, const tp_caps *caps,
                                       const char *name,
                                       int w, int h, u32 internal_format,
                                       const void *data, size_t size,
                                       tp_filter filter, tp_wrap wrap)
{
    memset(t, 0, sizeof(*t));
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "texture");

    tp_texfmt_family fam = tp_texfmt_family_of(internal_format);
    t->family = fam;

    /* The gate. Before any GL call, before any allocation: if the target
     * cannot sample this format, the asset is wrong and saying so here — with
     * the file's name — is worth far more than a working preview. */
    tp_result r = tp_caps_check_texfmt(caps, fam);
    if (r != TP_OK) {
        TP_ERROR("  ...while loading texture '%s' (%dx%d, %s)",
                 t->name, w, h, tp_texfmt_family_str(fam));
        return r;
    }

    r = check_size(caps, t->name, w, h);
    if (r != TP_OK) return r;

    /* A truncated or mis-declared level is a corrupt asset. glCompressedTexImage2D
     * would read past the buffer, so validate the size we can predict. */
    size_t expect = tp_texture_compressed_size(internal_format, w, h);
    if (expect && size != expect)
        return TP_FAIL(TP_ERR_ARGS,
                       "texture '%s': %s level is %zu bytes, expected %zu for "
                       "%dx%d — the asset is truncated or the header lies",
                       t->name, tp_texfmt_family_str(fam), size, expect, w, h);

    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0,
                           (GLsizei)size, data);

    /* glGenerateMipmap is not allowed on compressed textures. The converter
     * must ship the full mip chain; asking for trilinear here without one is
     * a content bug worth naming. */
    if (filter == TP_FILTER_TRILINEAR) {
        TP_WARN("texture '%s': trilinear requested but compressed textures "
                "cannot generate mipmaps at runtime — ship the mip chain in "
                "the KTX2. Falling back to bilinear.", t->name);
        filter = TP_FILTER_LINEAR;
    }
    apply_sampler_state(filter, wrap, false);
    glBindTexture(GL_TEXTURE_2D, 0);

    t->width = w;
    t->height = h;
    t->internal_format = internal_format;

    if (!tp_gl_check("tp_texture_create_compressed")) {
        tp_texture_destroy(t);
        return TP_ERR_GL;
    }
    TP_DEBUG("texture '%s': %dx%d %s, %zu bytes",
             t->name, w, h, tp_texfmt_family_str(fam), size);
    return TP_OK;
}

void tp_texture_destroy(tp_texture *t)
{
    if (t->id) glDeleteTextures(1, &t->id);
    memset(t, 0, sizeof(*t));
}

void tp_texture_bind(const tp_texture *t, int unit)
{
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, t ? t->id : 0);
}

void tp_texture_set_anisotropy(const tp_texture *t, const tp_caps *caps, f32 level)
{
    if (!caps || !caps->has_anisotropic || caps->max_anisotropy <= 1.0f) return;
    f32 l = TP_CLAMP(level, 1.0f, caps->max_anisotropy);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, l);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ---- procedural ------------------------------------------------------ */

static void write_rgba(u8 *p, u32 rgba)
{
    p[0] = (u8)(rgba >> 24);
    p[1] = (u8)(rgba >> 16);
    p[2] = (u8)(rgba >> 8);
    p[3] = (u8)(rgba);
}

tp_result tp_texture_make_checker(tp_texture *t, const tp_caps *caps,
                                  int size, int cells, u32 rgba_a, u32 rgba_b)
{
    size  = TP_CLAMP(size, 2, 1024);
    cells = TP_CLAMP(cells, 1, size);

    u8 *px = (u8 *)tp_alloc((size_t)size * (size_t)size * 4);
    if (!px) return TP_ERR_NOMEM;

    int cell_px = size / cells;
    if (cell_px < 1) cell_px = 1;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool on = ((x / cell_px) + (y / cell_px)) & 1;
            write_rgba(px + ((size_t)y * (size_t)size + (size_t)x) * 4,
                       on ? rgba_a : rgba_b);
        }
    }

    tp_result r = tp_texture_create_2d(t, caps, "checker", size, size,
                                       GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, px,
                                       TP_FILTER_TRILINEAR, TP_WRAP_REPEAT);
    tp_free(px);
    return r;
}

tp_result tp_texture_make_ramp(tp_texture *t, const tp_caps *caps,
                               int width, const u32 *stops, int count)
{
    width = TP_CLAMP(width, 2, 256);
    if (!stops || count < 2)
        return TP_FAIL(TP_ERR_ARGS, "ramp needs at least two stops");

    u8 *px = (u8 *)tp_alloc((size_t)width * 4);
    if (!px) return TP_ERR_NOMEM;

    for (int x = 0; x < width; ++x) {
        f32 u = (f32)x / (f32)(width - 1) * (f32)(count - 1);
        int i0 = (int)u;
        int i1 = TP_MIN(i0 + 1, count - 1);
        f32 f = u - (f32)i0;

        u8 out[4];
        for (int ch = 0; ch < 4; ++ch) {
            int shift = 24 - ch * 8;
            f32 a = (f32)((stops[i0] >> shift) & 0xFF);
            f32 b = (f32)((stops[i1] >> shift) & 0xFF);
            out[ch] = (u8)(tp_lerpf(a, b, f) + 0.5f);
        }
        memcpy(px + (size_t)x * 4, out, 4);
    }

    /* Clamp, not repeat: the ramp is indexed by a 0..1 lighting term and
     * wrapping it would make the darkest shade adjoin the brightest. */
    tp_result r = tp_texture_create_2d(t, caps, "ramp", width, 1,
                                       GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, px,
                                       TP_FILTER_LINEAR, TP_WRAP_CLAMP);
    tp_free(px);
    return r;
}
