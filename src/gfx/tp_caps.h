/* tp_caps.h — GL capability discovery and target-profile enforcement.
 *
 * This module exists because of one line in CONTRACT.md §2.1:
 *
 *   "Mali-G31 gives you ETC2 and ASTC. It does *not* give you S3TC/DXT/BCn,
 *    which is what desktop GPUs have. Anything shipping BCn textures will
 *    fail on-device."
 *
 * That class of bug — works on the dev box, dies on the board — is invisible
 * until you flash a card. The fix is to stop trusting the dev box. At startup
 * we build two capability sets:
 *
 *   actual   what this GPU really reports
 *   profile  what the *target* GPU (Mali-G31 / Panfrost) is known to have
 *
 * and every capability query in the renderer goes through the intersection.
 * On the board they are the same set. On a desktop the profile is strictly
 * smaller, so a desktop run refuses the things the board would refuse — at
 * the point of use, with a message naming the profile. `--profile native`
 * turns it off when you deliberately want to exercise desktop-only paths.
 */
#ifndef TP_CAPS_H
#define TP_CAPS_H

#include "../core/tp_common.h"

typedef enum {
    TP_PROFILE_MALI_G31 = 0, /* default: emulate the board's capability set */
    TP_PROFILE_NATIVE        /* trust whatever this GPU reports             */
} tp_profile;

/* Texture compression families we care about. */
typedef enum {
    TP_TEXFMT_UNCOMPRESSED = 0,
    TP_TEXFMT_ETC2,   /* core in GLES 3.0 — always available on target      */
    TP_TEXFMT_ASTC,   /* KHR_texture_compression_astc_ldr — G31 has it      */
    TP_TEXFMT_S3TC,   /* DXT/BCn — desktop only. THE trap.                  */
    TP_TEXFMT_PVRTC,  /* PowerVR — not on Mali                              */
    TP_TEXFMT_UNKNOWN
} tp_texfmt_family;

typedef struct {
    /* strings straight from the driver */
    const char *vendor;
    const char *renderer;
    const char *version;
    const char *glsl_version;
    char       *extensions;   /* space-joined, owned, NUL-terminated        */
    int         ext_count;

    /* parsed GLES version */
    int gl_major, gl_minor;

    /* limits that actually constrain the design */
    int max_texture_size;
    int max_cube_map_texture_size;
    int max_vertex_attribs;
    int max_vertex_uniform_vectors;
    int max_fragment_uniform_vectors;
    int max_varying_vectors;
    int max_uniform_block_size;        /* bone palette must fit here        */
    int max_vertex_uniform_blocks;
    int max_combined_texture_units;
    int max_samples;                   /* MSAA — CONTRACT §2.2 says measure */
    int max_renderbuffer_size;
    int num_compressed_formats;

    /* feature bits, already intersected with the active profile */
    bool has_etc2;
    bool has_astc_ldr;
    bool has_s3tc;                     /* on target: always false           */
    bool has_timer_query;              /* GL_EXT_disjoint_timer_query       */
    bool has_vao;                      /* core in 3.0, checked anyway       */
    bool has_instanced;
    bool has_float_texture_linear;
    bool has_depth_texture;
    bool has_anisotropic;
    f32  max_anisotropy;

    /* what the driver said before profile masking — for the log only */
    bool native_has_s3tc;
    bool native_has_astc_ldr;

    tp_profile profile;
    bool       is_panfrost;            /* running on the real thing         */
    bool       is_tiler;               /* tile-based deferred renderer      */
} tp_caps;

/* Query the current context. Requires a current GL context. */
tp_result tp_caps_query(tp_caps *caps, tp_profile profile);
void tp_caps_free(tp_caps *caps);

/* Raw extension test — ignores the profile. Use tp_caps fields instead unless
 * you specifically want to know what the hardware said. */
bool tp_caps_has_extension(const tp_caps *caps, const char *name);

/* Log the full capability block at INFO. This is what you paste into docs/
 * when the board finally boots. */
void tp_caps_log(const tp_caps *caps);

/* Profile gate for texture formats. Returns TP_ERR_CAPS and logs a message
 * naming the offending format when the family is unavailable on the target.
 * Every texture upload path must call this — that is the whole point. */
tp_result tp_caps_check_texfmt(const tp_caps *caps, tp_texfmt_family fam);

/* Classify a GL internal format into a family. */
tp_texfmt_family tp_texfmt_family_of(u32 gl_internal_format);
const char *tp_texfmt_family_str(tp_texfmt_family fam);

/* Assert the minimum the renderer needs; logs everything missing at once
 * rather than failing on the first. */
tp_result tp_caps_require_baseline(const tp_caps *caps);

const char *tp_profile_str(tp_profile p);
tp_profile  tp_profile_parse(const char *s); /* returns MALI_G31 on garbage */

#endif /* TP_CAPS_H */
