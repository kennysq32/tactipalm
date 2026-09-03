/* tp_shader.c — GLSL ES 3.00 program objects. */
#include "tp_shader.h"
#include "tp_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pinned preamble. `highp` in the fragment stage is not free on a Mali tiler,
 * so the default fragment float precision is mediump and anything needing
 * more must say so per-declaration — which makes the cost visible at the
 * point where it is paid. */
static const char *k_preamble =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "#else\n"
    "#error target lacks highp in fragment stage\n"
    "#endif\n";

static const char *k_frag_precision =
    "precision mediump float;\n"
    "precision highp int;\n";

static const char *stage_name(u32 type)
{
    return type == GL_VERTEX_SHADER ? "vertex" : "fragment";
}

/* Trim, then reject a #version the caller should not have written. Returns
 * false and logs when the source is not acceptable. */
static bool validate_source(const char *name, const char *src, u32 type)
{
    const char *v = strstr(src, "#version");
    if (!v) return true;

    /* Ignore one inside a line comment — rare, but a confusing failure. */
    TP_ERROR("%s %s shader carries its own '#version' directive. Remove it: "
             "tp_shader pins '#version 300 es' for every program so that a "
             "3.10-only shader cannot compile on the dev box and then fail on "
             "Mali-G31 (CONTRACT.md §2).", name, stage_name(type));
    return false;
}

static bool compile_stage(const char *name, u32 type, const char *src, GLuint *out)
{
    if (!validate_source(name, src, type)) return false;

    GLuint sh = glCreateShader(type);
    if (!sh) { TP_ERROR("glCreateShader failed for %s", name); return false; }

    const char *parts[3];
    GLint lens[3];
    int n = 0;

    parts[n] = k_preamble; lens[n] = (GLint)strlen(k_preamble); n++;
    if (type == GL_FRAGMENT_SHADER) {
        parts[n] = k_frag_precision; lens[n] = (GLint)strlen(k_frag_precision); n++;
    }
    parts[n] = src; lens[n] = (GLint)strlen(src); n++;

    glShaderSource(sh, n, parts, lens);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)tp_alloc((size_t)(len > 0 ? len : 1) + 1);
        if (log) {
            glGetShaderInfoLog(sh, len, NULL, log);
            TP_ERROR("%s %s shader failed to compile:\n%s",
                     name, stage_name(type), log);
            tp_free(log);
        }
        glDeleteShader(sh);
        return false;
    }

    GLint len = 0;
    glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
        char *log = (char *)tp_alloc((size_t)len + 1);
        if (log) {
            glGetShaderInfoLog(sh, len, NULL, log);
            /* Panfrost is chattier than Mesa's desktop drivers; warnings here
             * are usually the ones worth reading. */
            TP_DEBUG("%s %s shader log: %s", name, stage_name(type), log);
            tp_free(log);
        }
    }

    *out = sh;
    return true;
}

tp_result tp_shader_create(tp_shader *sh, const tp_caps *caps, const char *name,
                           const char *vert_src, const char *frag_src)
{
    memset(sh, 0, sizeof(*sh));
    snprintf(sh->name, sizeof(sh->name), "%s", name ? name : "shader");

    if (caps && caps->gl_major < 3)
        return TP_FAIL(TP_ERR_CAPS,
                       "GLSL ES 3.00 needs a GLES 3.0 context, have %d.%d",
                       caps->gl_major, caps->gl_minor);

    GLuint vs = 0, fs = 0;
    if (!compile_stage(sh->name, GL_VERTEX_SHADER, vert_src, &vs))
        return TP_ERR_GL;
    if (!compile_stage(sh->name, GL_FRAGMENT_SHADER, frag_src, &fs)) {
        glDeleteShader(vs);
        return TP_ERR_GL;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    /* Attached shaders are refcounted; flag them now so the program owns the
     * only reference and teardown is a single glDeleteProgram. */
    glDetachShader(prog, vs);
    glDetachShader(prog, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)tp_alloc((size_t)(len > 0 ? len : 1) + 1);
        if (log) {
            glGetProgramInfoLog(prog, len, NULL, log);
            TP_ERROR("%s program failed to link:\n%s", sh->name, log);
            tp_free(log);
        }
        glDeleteProgram(prog);
        return TP_ERR_GL;
    }

    sh->program = prog;
    TP_DEBUG("shader '%s' linked (program %u)", sh->name, prog);
    return TP_OK;
}

tp_result tp_shader_create_from_files(tp_shader *sh, const tp_caps *caps,
                                      const char *name,
                                      const char *vert_path, const char *frag_path)
{
    char *v = tp_read_file(vert_path, NULL);
    if (!v) return TP_FAIL(TP_ERR_IO, "cannot read vertex shader '%s'", vert_path);
    char *f = tp_read_file(frag_path, NULL);
    if (!f) { tp_free(v); return TP_FAIL(TP_ERR_IO, "cannot read fragment shader '%s'", frag_path); }

    tp_result r = tp_shader_create(sh, caps, name, v, f);
    tp_free(v);
    tp_free(f);
    return r;
}

void tp_shader_destroy(tp_shader *sh)
{
    if (sh->program) glDeleteProgram(sh->program);
    memset(sh, 0, sizeof(*sh));
}

void tp_shader_bind(const tp_shader *sh) { glUseProgram(sh->program); }

i32 tp_shader_uniform(tp_shader *sh, const char *name)
{
    for (int i = 0; i < sh->uniform_count; ++i)
        if (strcmp(sh->uniforms[i].name, name) == 0)
            return sh->uniforms[i].location;

    i32 loc = glGetUniformLocation(sh->program, name);

    if (sh->uniform_count < TP_SHADER_MAX_UNIFORMS) {
        tp_uniform_entry *e = &sh->uniforms[sh->uniform_count++];
        snprintf(e->name, sizeof(e->name), "%s", name);
        e->location = loc;
    } else {
        /* Silently falling back to an uncached glGetUniformLocation every
         * frame would be a quiet per-frame cost; say so once. */
        static bool warned = false;
        if (!warned) {
            TP_WARN("shader '%s' exceeded the %d-entry uniform cache; "
                    "raise TP_SHADER_MAX_UNIFORMS", sh->name,
                    TP_SHADER_MAX_UNIFORMS);
            warned = true;
        }
    }

    if (loc < 0)
        TP_TRACE("shader '%s': uniform '%s' not active", sh->name, name);
    return loc;
}

void tp_shader_set_f1(tp_shader *sh, const char *name, f32 v)
{ i32 l = tp_shader_uniform(sh, name); if (l >= 0) glUniform1f(l, v); }

void tp_shader_set_i1(tp_shader *sh, const char *name, i32 v)
{ i32 l = tp_shader_uniform(sh, name); if (l >= 0) glUniform1i(l, v); }

void tp_shader_set_v2(tp_shader *sh, const char *name, tp_vec2 v)
{ i32 l = tp_shader_uniform(sh, name); if (l >= 0) glUniform2f(l, v.x, v.y); }

void tp_shader_set_v3(tp_shader *sh, const char *name, tp_vec3 v)
{ i32 l = tp_shader_uniform(sh, name); if (l >= 0) glUniform3f(l, v.x, v.y, v.z); }

void tp_shader_set_v4(tp_shader *sh, const char *name, tp_vec4 v)
{ i32 l = tp_shader_uniform(sh, name); if (l >= 0) glUniform4f(l, v.x, v.y, v.z, v.w); }

void tp_shader_set_mat4(tp_shader *sh, const char *name, const tp_mat4 *m)
{
    i32 l = tp_shader_uniform(sh, name);
    /* Column-major storage, hence transpose = GL_FALSE — and GLES 3.0 would
     * reject GL_TRUE anyway. */
    if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m->e);
}

void tp_shader_set_mat4_array(tp_shader *sh, const char *name,
                              const tp_mat4 *m, int count)
{
    i32 l = tp_shader_uniform(sh, name);
    if (l >= 0) glUniformMatrix4fv(l, count, GL_FALSE, m->e);
}

tp_result tp_shader_bind_ubo(tp_shader *sh, const char *block_name, u32 binding)
{
    GLuint idx = glGetUniformBlockIndex(sh->program, block_name);
    if (idx == GL_INVALID_INDEX)
        return TP_FAIL(TP_ERR_GL, "shader '%s' has no uniform block '%s'",
                       sh->name, block_name);
    glUniformBlockBinding(sh->program, idx, binding);
    return TP_OK;
}
