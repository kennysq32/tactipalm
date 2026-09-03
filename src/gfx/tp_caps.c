/* tp_caps.c — GL capability discovery and target-profile enforcement. */
#include "tp_caps.h"
#include "tp_gl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------- */
/* The target profile.                                                    */
/*                                                                        */
/* Two kinds of entry. Hard denials are things Mali-G31 provably does not  */
/* have; those are masked off unconditionally. Limits are clamped to the   */
/* GLES 3.0 *spec minimums* rather than to guessed Mali numbers — a        */
/* conservative bound we can defend, and one that costs nothing since the  */
/* content budget in PLAN.md §6 sits well inside it. When the board boots  */
/* and docs/ gets the real extension string and limits, these become       */
/* measurements instead of bounds.                                        */
/* ---------------------------------------------------------------------- */
#define TP_G31_MAX_TEXTURE_SIZE            4096
#define TP_G31_MAX_SAMPLES                    4
#define TP_G31_MAX_UNIFORM_BLOCK_SIZE     16384  /* GLES 3.0 minimum        */
#define TP_G31_MAX_VERTEX_UNIFORM_VECTORS   256  /* GLES 3.0 minimum        */
#define TP_G31_MAX_FRAGMENT_UNIFORM_VECTORS 224  /* GLES 3.0 minimum        */
#define TP_G31_MAX_VARYING_VECTORS           15  /* GLES 3.0 minimum        */
#define TP_G31_MAX_VERTEX_ATTRIBS            16  /* GLES 3.0 minimum        */
#define TP_G31_MAX_VERTEX_UNIFORM_BLOCKS     12  /* GLES 3.0 minimum        */
#define TP_G31_MAX_COMBINED_TEXTURE_UNITS    32  /* GLES 3.0 minimum        */

const char *tp_profile_str(tp_profile p)
{
    switch (p) {
    case TP_PROFILE_MALI_G31: return "mali-g31";
    case TP_PROFILE_NATIVE:   return "native";
    }
    return "?";
}

tp_profile tp_profile_parse(const char *s)
{
    if (!s) return TP_PROFILE_MALI_G31;
    if (strcmp(s, "native") == 0) return TP_PROFILE_NATIVE;
    if (strcmp(s, "mali-g31") == 0 || strcmp(s, "mali") == 0 ||
        strcmp(s, "target") == 0 || strcmp(s, "g31") == 0)
        return TP_PROFILE_MALI_G31;
    TP_WARN("unknown profile '%s', using 'mali-g31'", s);
    return TP_PROFILE_MALI_G31;
}

const char *tp_texfmt_family_str(tp_texfmt_family fam)
{
    switch (fam) {
    case TP_TEXFMT_UNCOMPRESSED: return "uncompressed";
    case TP_TEXFMT_ETC2:         return "ETC2/EAC";
    case TP_TEXFMT_ASTC:         return "ASTC LDR";
    case TP_TEXFMT_S3TC:         return "S3TC/DXT/BCn";
    case TP_TEXFMT_PVRTC:        return "PVRTC";
    case TP_TEXFMT_UNKNOWN:      return "unknown";
    }
    return "?";
}

tp_texfmt_family tp_texfmt_family_of(u32 fmt)
{
    switch (fmt) {
    /* ETC2 / EAC — core in GLES 3.0. */
    case GL_COMPRESSED_RGB8_ETC2:
    case GL_COMPRESSED_SRGB8_ETC2:
    case GL_COMPRESSED_RGBA8_ETC2_EAC:
    case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
    case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
    case GL_COMPRESSED_R11_EAC:
    case GL_COMPRESSED_SIGNED_R11_EAC:
    case GL_COMPRESSED_RG11_EAC:
    case GL_COMPRESSED_SIGNED_RG11_EAC:
        return TP_TEXFMT_ETC2;

    /* ASTC LDR. The block-size enums are contiguous in both the linear and
     * sRGB ranges, so a range test is clearer than 28 case labels. */
    default: break;
    }

    if ((fmt >= GL_COMPRESSED_RGBA_ASTC_4x4_KHR &&
         fmt <= GL_COMPRESSED_RGBA_ASTC_12x12_KHR) ||
        (fmt >= GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR &&
         fmt <= GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR))
        return TP_TEXFMT_ASTC;

    switch (fmt) {
    case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        return TP_TEXFMT_S3TC;
    case GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG:
    case GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG:
    case GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG:
    case GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG:
        return TP_TEXFMT_PVRTC;
    default:
        break;
    }

    /* Everything not compressed falls here; callers pass uncompressed
     * sized internal formats too. */
    return TP_TEXFMT_UNCOMPRESSED;
}

/* ---------------------------------------------------------------------- */

bool tp_caps_has_extension(const tp_caps *caps, const char *name)
{
    if (!caps || !caps->extensions || !name) return false;
    size_t nlen = strlen(name);
    const char *p = caps->extensions;
    while ((p = strstr(p, name)) != NULL) {
        bool left_ok  = (p == caps->extensions) || p[-1] == ' ';
        bool right_ok = (p[nlen] == ' ' || p[nlen] == '\0');
        if (left_ok && right_ok) return true;
        p += nlen;
    }
    return false;
}

static char *collect_extensions(int *out_count)
{
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);

    /* GL_NUM_EXTENSIONS is 3.0+. If the enum was rejected we are on a context
     * older than we asked for; fall back to the monolithic string. */
    if (glGetError() != GL_NO_ERROR || n <= 0) {
        const char *s = (const char *)glGetString(GL_EXTENSIONS);
        glGetError();
        if (!s) { *out_count = 0; return NULL; }
        int count = 1;
        for (const char *p = s; *p; ++p) if (*p == ' ') count++;
        *out_count = count;
        return strdup(s);
    }

    size_t total = 1;
    for (GLint i = 0; i < n; ++i) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e) total += strlen(e) + 1;
    }

    char *buf = (char *)tp_alloc(total);
    if (!buf) { *out_count = 0; return NULL; }

    size_t off = 0;
    for (GLint i = 0; i < n; ++i) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (!e) continue;
        size_t l = strlen(e);
        if (off) buf[off++] = ' ';
        memcpy(buf + off, e, l);
        off += l;
    }
    buf[off] = '\0';
    *out_count = (int)n;
    return buf;
}

static void parse_gl_version(const char *ver, int *major, int *minor)
{
    *major = 0; *minor = 0;
    if (!ver) return;
    /* "OpenGL ES 3.2 Mesa 26.1.6" — skip to the first digit. */
    const char *p = ver;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    *major = atoi(p);
    const char *dot = strchr(p, '.');
    if (dot) *minor = atoi(dot + 1);
}

static int geti(GLenum pname)
{
    GLint v = 0;
    glGetIntegerv(pname, &v);
    if (glGetError() != GL_NO_ERROR) return 0;
    return (int)v;
}

static int clamp_limit(const char *name, int actual, int cap, bool apply)
{
    if (!apply || actual <= cap) return actual;
    TP_DEBUG("profile: %s %d -> %d (clamped to target bound)", name, actual, cap);
    return cap;
}

tp_result tp_caps_query(tp_caps *caps, tp_profile profile)
{
    memset(caps, 0, sizeof(*caps));
    caps->profile = profile;

    caps->vendor       = (const char *)glGetString(GL_VENDOR);
    caps->renderer     = (const char *)glGetString(GL_RENDERER);
    caps->version      = (const char *)glGetString(GL_VERSION);
    caps->glsl_version = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

    if (!caps->version)
        return TP_FAIL(TP_ERR_GL, "no current GL context (glGetString returned NULL)");

    parse_gl_version(caps->version, &caps->gl_major, &caps->gl_minor);
    caps->extensions = collect_extensions(&caps->ext_count);

    caps->is_panfrost =
        (caps->renderer && (strstr(caps->renderer, "Mali") ||
                            strstr(caps->renderer, "panfrost") ||
                            strstr(caps->renderer, "Panfrost")));
    /* Only Panfrost is confirmed tile-based here; the flag exists so render
     * passes can pick the cheap path, not to make policy decisions. */
    caps->is_tiler = caps->is_panfrost;

    const bool mask = (profile == TP_PROFILE_MALI_G31);

    /* ---- limits ---- */
    caps->max_texture_size = clamp_limit("MAX_TEXTURE_SIZE",
        geti(GL_MAX_TEXTURE_SIZE), TP_G31_MAX_TEXTURE_SIZE, mask);
    caps->max_cube_map_texture_size = clamp_limit("MAX_CUBE_MAP_TEXTURE_SIZE",
        geti(GL_MAX_CUBE_MAP_TEXTURE_SIZE), TP_G31_MAX_TEXTURE_SIZE, mask);
    caps->max_vertex_attribs = clamp_limit("MAX_VERTEX_ATTRIBS",
        geti(GL_MAX_VERTEX_ATTRIBS), TP_G31_MAX_VERTEX_ATTRIBS, mask);
    caps->max_vertex_uniform_vectors = clamp_limit("MAX_VERTEX_UNIFORM_VECTORS",
        geti(GL_MAX_VERTEX_UNIFORM_VECTORS), TP_G31_MAX_VERTEX_UNIFORM_VECTORS, mask);
    caps->max_fragment_uniform_vectors = clamp_limit("MAX_FRAGMENT_UNIFORM_VECTORS",
        geti(GL_MAX_FRAGMENT_UNIFORM_VECTORS), TP_G31_MAX_FRAGMENT_UNIFORM_VECTORS, mask);
    caps->max_varying_vectors = clamp_limit("MAX_VARYING_VECTORS",
        geti(GL_MAX_VARYING_VECTORS), TP_G31_MAX_VARYING_VECTORS, mask);
    caps->max_uniform_block_size = clamp_limit("MAX_UNIFORM_BLOCK_SIZE",
        geti(GL_MAX_UNIFORM_BLOCK_SIZE), TP_G31_MAX_UNIFORM_BLOCK_SIZE, mask);
    caps->max_vertex_uniform_blocks = clamp_limit("MAX_VERTEX_UNIFORM_BLOCKS",
        geti(GL_MAX_VERTEX_UNIFORM_BLOCKS), TP_G31_MAX_VERTEX_UNIFORM_BLOCKS, mask);
    caps->max_combined_texture_units = clamp_limit("MAX_COMBINED_TEXTURE_IMAGE_UNITS",
        geti(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS), TP_G31_MAX_COMBINED_TEXTURE_UNITS, mask);
    caps->max_samples = clamp_limit("MAX_SAMPLES",
        geti(GL_MAX_SAMPLES), TP_G31_MAX_SAMPLES, mask);
    caps->max_renderbuffer_size = geti(GL_MAX_RENDERBUFFER_SIZE);
    caps->num_compressed_formats = geti(GL_NUM_COMPRESSED_TEXTURE_FORMATS);

    /* ---- features ---- */
    caps->native_has_s3tc =
        tp_caps_has_extension(caps, "GL_EXT_texture_compression_s3tc") ||
        tp_caps_has_extension(caps, "GL_EXT_texture_compression_dxt1") ||
        tp_caps_has_extension(caps, "GL_ANGLE_texture_compression_dxt5");
    caps->native_has_astc_ldr =
        tp_caps_has_extension(caps, "GL_KHR_texture_compression_astc_ldr") ||
        tp_caps_has_extension(caps, "GL_OES_texture_compression_astc");

    /* ETC2 is core in GLES 3.0 — no extension to test, the version is the
     * test. Desktop GL drivers must accept the formats too (they may decode
     * in software, which is fine for a dev box and irrelevant on target). */
    caps->has_etc2 = (caps->gl_major >= 3);

    caps->has_astc_ldr = caps->native_has_astc_ldr;

    /* The hard denial. On the target this extension does not exist, so the
     * masked value is the truthful one; masking it off on desktop is what
     * stops a BCn asset from ever entering the pipeline. */
    caps->has_s3tc = mask ? false : caps->native_has_s3tc;

    /* CONTRACT.md §2.4 names this one specifically: query, never assume. */
    caps->has_timer_query =
        tp_caps_has_extension(caps, "GL_EXT_disjoint_timer_query");

    caps->has_vao       = (caps->gl_major >= 3);
    caps->has_instanced = (caps->gl_major >= 3);
    caps->has_depth_texture = (caps->gl_major >= 3);
    caps->has_float_texture_linear =
        tp_caps_has_extension(caps, "GL_OES_texture_float_linear");

    caps->has_anisotropic =
        tp_caps_has_extension(caps, "GL_EXT_texture_filter_anisotropic");
    if (caps->has_anisotropic) {
        GLfloat a = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &a);
        if (glGetError() != GL_NO_ERROR) a = 1.0f;
        /* Anisotropy is a bandwidth multiplier, and bandwidth is the budget
         * on a tiler (CONTRACT.md §2.2). Cap it hard under the profile. */
        caps->max_anisotropy = mask ? TP_MIN(a, 4.0f) : a;
    } else {
        caps->max_anisotropy = 1.0f;
    }

    glGetError(); /* leave the queue clean for the renderer */
    return TP_OK;
}

void tp_caps_free(tp_caps *caps)
{
    if (!caps) return;
    tp_free(caps->extensions);
    caps->extensions = NULL;
    caps->ext_count = 0;
}

void tp_caps_log(const tp_caps *caps)
{
    TP_INFO("GL vendor    : %s", caps->vendor   ? caps->vendor   : "(null)");
    TP_INFO("GL renderer  : %s", caps->renderer ? caps->renderer : "(null)");
    TP_INFO("GL version   : %s", caps->version  ? caps->version  : "(null)");
    TP_INFO("GLSL version : %s", caps->glsl_version ? caps->glsl_version : "(null)");
    TP_INFO("profile      : %s%s", tp_profile_str(caps->profile),
            caps->profile == TP_PROFILE_MALI_G31
                ? " (target capability set enforced)" : " (no masking)");
    TP_INFO("tiler        : %s", caps->is_tiler ? "yes (Panfrost)"
                                                : "no / unknown");
    TP_INFO("limits       : tex %d  attribs %d  varying %d  vs-uniforms %d",
            caps->max_texture_size, caps->max_vertex_attribs,
            caps->max_varying_vectors, caps->max_vertex_uniform_vectors);
    TP_INFO("               ubo-max %d B  vs-ubos %d  max-samples %d  units %d",
            caps->max_uniform_block_size, caps->max_vertex_uniform_blocks,
            caps->max_samples, caps->max_combined_texture_units);
    TP_INFO("texfmt       : ETC2 %s  ASTC %s  S3TC %s",
            caps->has_etc2 ? "yes" : "NO",
            caps->has_astc_ldr ? "yes" : "no",
            caps->has_s3tc ? "yes" : "no (correct for target)");
    TP_INFO("features     : timer-query %s  aniso %.0fx  float-linear %s",
            caps->has_timer_query ? "yes" : "no",
            (double)caps->max_anisotropy,
            caps->has_float_texture_linear ? "yes" : "no");
    TP_INFO("extensions   : %d reported", caps->ext_count);

    if (caps->profile == TP_PROFILE_MALI_G31 && caps->native_has_s3tc)
        TP_INFO("note: this GPU exposes S3TC but the profile hides it — "
                "an S3TC texture will be refused here exactly as it would "
                "be on the board");
    if (caps->profile == TP_PROFILE_MALI_G31 && !caps->native_has_astc_ldr)
        TP_WARN("this GPU has no ASTC; ASTC assets cannot be previewed here, "
                "though the board will accept them. Use ETC2 for dev.");
    TP_DEBUG("extension string: %s", caps->extensions ? caps->extensions : "");
}

tp_result tp_caps_check_texfmt(const tp_caps *caps, tp_texfmt_family fam)
{
    switch (fam) {
    case TP_TEXFMT_UNCOMPRESSED:
        return TP_OK;
    case TP_TEXFMT_ETC2:
        if (caps->has_etc2) return TP_OK;
        return TP_FAIL(TP_ERR_CAPS,
                       "ETC2 unavailable — needs a GLES 3.0 context (have %d.%d)",
                       caps->gl_major, caps->gl_minor);
    case TP_TEXFMT_ASTC:
        if (caps->has_astc_ldr) return TP_OK;
        return TP_FAIL(TP_ERR_CAPS,
                       "ASTC unavailable on this GPU. The target supports it, so "
                       "this asset is fine to ship — but it cannot be previewed "
                       "here. Convert to ETC2 for desktop work.");
    case TP_TEXFMT_S3TC:
        if (caps->has_s3tc) return TP_OK;
        return TP_FAIL(TP_ERR_CAPS,
                       "S3TC/DXT/BCn texture refused. Mali-G31 has no BCn support, "
                       "so this asset would fail on the Orange Pi even though the "
                       "dev GPU can load it (CONTRACT.md §2.1). Re-encode as ETC2 "
                       "or ASTC in a KTX2 container. Override with --profile native "
                       "only if you know why.");
    case TP_TEXFMT_PVRTC:
        return TP_FAIL(TP_ERR_CAPS,
                       "PVRTC texture refused — PowerVR format, not supported by "
                       "Mali-G31. Re-encode as ETC2 or ASTC.");
    case TP_TEXFMT_UNKNOWN:
    default:
        return TP_FAIL(TP_ERR_CAPS, "unrecognised texture format family");
    }
}

tp_result tp_caps_require_baseline(const tp_caps *caps)
{
    int missing = 0;

    if (caps->gl_major < 3) {
        TP_ERROR("need OpenGL ES 3.0 or better, got %d.%d",
                 caps->gl_major, caps->gl_minor);
        missing++;
    }
    if (!caps->has_etc2) {
        TP_ERROR("ETC2 texture support absent (core in GLES 3.0)");
        missing++;
    }
    if (!caps->has_vao) {
        TP_ERROR("vertex array objects absent (core in GLES 3.0)");
        missing++;
    }
    /* The bone palette is the sizing constraint the renderer cannot work
     * around: PLAN.md §7 budgets 64 bones as mat4 in a UBO. */
    const int bone_palette_bytes = 64 * 16 * (int)sizeof(float);
    if (caps->max_uniform_block_size < bone_palette_bytes) {
        TP_ERROR("MAX_UNIFORM_BLOCK_SIZE is %d B, need >= %d B for a 64-bone "
                 "mat4 palette", caps->max_uniform_block_size, bone_palette_bytes);
        missing++;
    }
    if (caps->max_vertex_uniform_blocks < 2) {
        TP_ERROR("MAX_VERTEX_UNIFORM_BLOCKS is %d, need >= 2 (camera + bones)",
                 caps->max_vertex_uniform_blocks);
        missing++;
    }

    if (missing)
        return TP_FAIL(TP_ERR_CAPS, "%d baseline requirement(s) unmet", missing);

    TP_DEBUG("baseline capability check passed");
    return TP_OK;
}
