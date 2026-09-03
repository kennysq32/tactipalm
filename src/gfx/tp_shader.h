/* tp_shader.h — GLSL ES 3.00 program objects.
 *
 * Every program is compiled against a fixed preamble that pins `#version 300
 * es` and the default precision qualifiers. Shader sources must therefore
 * *not* carry their own #version line — the loader rejects one that does,
 * because a stray `#version 310 es` compiling happily on a desktop driver is
 * precisely the failure this framework is built to make impossible.
 */
#ifndef TP_SHADER_H
#define TP_SHADER_H

#include "../core/tp_common.h"
#include "../core/tp_math.h"
#include "tp_caps.h"

#define TP_SHADER_MAX_UNIFORMS 48

typedef struct {
    char name[64];
    i32  location;
} tp_uniform_entry;

typedef struct {
    u32  program;
    char name[48];
    tp_uniform_entry uniforms[TP_SHADER_MAX_UNIFORMS];
    int  uniform_count;
} tp_shader;

/* Compile and link. `name` is used in log messages only. Sources are the
 * shader bodies without a #version directive. */
tp_result tp_shader_create(tp_shader *sh, const tp_caps *caps, const char *name,
                           const char *vert_src, const char *frag_src);
/* Same, reading both stages from disk. Convenient during development; the
 * shipped build embeds sources instead. */
tp_result tp_shader_create_from_files(tp_shader *sh, const tp_caps *caps,
                                      const char *name,
                                      const char *vert_path, const char *frag_path);
void tp_shader_destroy(tp_shader *sh);
void tp_shader_bind(const tp_shader *sh);

/* Cached glGetUniformLocation. Returns -1 for an absent (or optimised-out)
 * uniform, which the setters below treat as a no-op. */
i32 tp_shader_uniform(tp_shader *sh, const char *name);

void tp_shader_set_f1(tp_shader *sh, const char *name, f32 v);
void tp_shader_set_v2(tp_shader *sh, const char *name, tp_vec2 v);
void tp_shader_set_v3(tp_shader *sh, const char *name, tp_vec3 v);
void tp_shader_set_v4(tp_shader *sh, const char *name, tp_vec4 v);
void tp_shader_set_i1(tp_shader *sh, const char *name, i32 v);
void tp_shader_set_mat4(tp_shader *sh, const char *name, const tp_mat4 *m);
void tp_shader_set_mat4_array(tp_shader *sh, const char *name,
                              const tp_mat4 *m, int count);

/* Bind a named uniform block to a binding point. UBOs are the only route to
 * the bone palette on this target — SSBOs are off the table (CONTRACT §2). */
tp_result tp_shader_bind_ubo(tp_shader *sh, const char *block_name, u32 binding);

#endif /* TP_SHADER_H */
