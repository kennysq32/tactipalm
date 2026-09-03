/* tp_texture.h — texture objects, with the target format gate in front.
 *
 * Every path that uploads compressed pixel data runs tp_caps_check_texfmt
 * first. On the dev box, with the default mali-g31 profile, that means an
 * S3TC/DXT/BCn texture is refused here exactly as Mali-G31 would refuse it —
 * which is the entire reason the profile exists. CONTRACT.md §2.1 flags this
 * as the failure that "bites silently"; this module is where it stops being
 * silent.
 */
#ifndef TP_TEXTURE_H
#define TP_TEXTURE_H

#include "../core/tp_common.h"
#include "../core/tp_math.h"
#include "tp_caps.h"

typedef enum {
    TP_FILTER_NEAREST = 0,
    TP_FILTER_LINEAR,
    TP_FILTER_TRILINEAR   /* linear + mipmaps */
} tp_filter;

typedef enum {
    TP_WRAP_REPEAT = 0,
    TP_WRAP_CLAMP,
    TP_WRAP_MIRROR
} tp_wrap;

typedef struct {
    u32  id;
    int  width, height;
    u32  internal_format;
    tp_texfmt_family family;
    bool has_mipmaps;
    char name[32];
} tp_texture;

/* Uncompressed upload. `internal_format` is a sized GLES 3.0 format such as
 * GL_RGBA8 or GL_R8. */
tp_result tp_texture_create_2d(tp_texture *t, const tp_caps *caps,
                               const char *name,
                               int w, int h, u32 internal_format,
                               u32 format, u32 type, const void *pixels,
                               tp_filter filter, tp_wrap wrap);

/* Compressed upload of a single mip level. Gated on tp_caps_check_texfmt. */
tp_result tp_texture_create_compressed(tp_texture *t, const tp_caps *caps,
                                       const char *name,
                                       int w, int h, u32 internal_format,
                                       const void *data, size_t size,
                                       tp_filter filter, tp_wrap wrap);

void tp_texture_destroy(tp_texture *t);
void tp_texture_bind(const tp_texture *t, int unit);
/* Apply anisotropy if the profile allows any. No-op at 1x. */
void tp_texture_set_anisotropy(const tp_texture *t, const tp_caps *caps, f32 level);

/* Procedural sources, so the renderer has something to sample before the
 * asset converter exists. Both are small and generated once at load. */
tp_result tp_texture_make_checker(tp_texture *t, const tp_caps *caps,
                                  int size, int cells,
                                  u32 rgba_a, u32 rgba_b);
/* 1-D shading ramp as a width x 1 texture, for the stylised lighting response
 * described in PLAN.md §7. `stops` is an array of `count` RGBA words sampled
 * evenly across the ramp and interpolated. */
tp_result tp_texture_make_ramp(tp_texture *t, const tp_caps *caps,
                               int width, const u32 *stops, int count);

/* Bytes a compressed level occupies, for validating an asset before upload.
 * Returns 0 for a format this build does not know how to size. */
size_t tp_texture_compressed_size(u32 internal_format, int w, int h);

#endif /* TP_TEXTURE_H */
