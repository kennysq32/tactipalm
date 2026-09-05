/* tp_render.c — shared uniform buffers, frame state, GPU timing. */
#include "tp_render.h"
#include "tp_gl.h"

#include <string.h>

tp_result tp_render_init(tp_render *r, const tp_caps *caps)
{
    memset(r, 0, sizeof(*r));

    if ((int)sizeof(tp_bone_block) > caps->max_uniform_block_size)
        return TP_FAIL(TP_ERR_CAPS,
                       "bone palette is %zu B but MAX_UNIFORM_BLOCK_SIZE is %d",
                       sizeof(tp_bone_block), caps->max_uniform_block_size);

    glGenBuffers(1, &r->camera_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->camera_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(tp_camera_block), NULL, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &r->bone_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, r->bone_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(tp_bone_block), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glBindBufferBase(GL_UNIFORM_BUFFER, TP_UBO_CAMERA, r->camera_ubo);
    glBindBufferBase(GL_UNIFORM_BUFFER, TP_UBO_BONES,  r->bone_ubo);

    /* An identity palette means an unskinned draw still produces the rest
     * pose rather than collapsing to the origin if a shader takes the skinned
     * branch before anything animates it. */
    for (int i = 0; i < TP_MAX_BONES; ++i)
        r->bones.bones[i] = tp_mat4_identity();
    r->bone_count = 0;
    r->bones_dirty = true;
    r->camera_dirty = true;

    if (!tp_gl_check("tp_render_init")) {
        tp_render_shutdown(r);
        return TP_ERR_GL;
    }
    TP_DEBUG("render: camera UBO %zu B, bone UBO %zu B (%d bones max)",
             sizeof(tp_camera_block), sizeof(tp_bone_block), TP_MAX_BONES);
    return TP_OK;
}

void tp_render_shutdown(tp_render *r)
{
    if (r->camera_ubo) glDeleteBuffers(1, &r->camera_ubo);
    if (r->bone_ubo)   glDeleteBuffers(1, &r->bone_ubo);
    memset(r, 0, sizeof(*r));
}

tp_result tp_render_bind_blocks(tp_render *r, tp_shader *sh, bool wants_bones)
{
    TP_UNUSED(r);
    tp_result res = tp_shader_bind_ubo(sh, "Camera", TP_UBO_CAMERA);
    if (res != TP_OK) return res;
    if (wants_bones) {
        res = tp_shader_bind_ubo(sh, "Bones", TP_UBO_BONES);
        if (res != TP_OK) return res;
    }
    return TP_OK;
}

void tp_render_set_camera(tp_render *r, tp_mat4 view_proj, tp_vec3 eye,
                          tp_vec3 light_dir)
{
    r->camera.view_proj  = view_proj;
    r->camera.camera_pos = tp_v4(eye.x, eye.y, eye.z, 1.0f);
    tp_vec3 l = tp_v3_norm(light_dir);
    r->camera.light_dir  = tp_v4(l.x, l.y, l.z, 0.0f);
    r->camera_dirty = true;
}

void tp_render_set_bone(tp_render *r, int index, tp_mat4 m)
{
    if (index < 0 || index >= TP_MAX_BONES) return;
    r->bones.bones[index] = m;
    r->bones_dirty = true;
}

void tp_render_set_bone_count(tp_render *r, int count)
{
    r->bone_count = TP_CLAMP(count, 0, TP_MAX_BONES);
}

void tp_render_flush(tp_render *r)
{
    if (r->camera_dirty) {
        glBindBuffer(GL_UNIFORM_BUFFER, r->camera_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(tp_camera_block), &r->camera);
        r->camera_dirty = false;
    }
    if (r->bones_dirty) {
        /* Upload only the bones in use. On a 64-bone budget the difference is
         * 4 KiB versus a few hundred bytes per frame — small in isolation,
         * but it is the same LPDDR4 the display controller is scanning out
         * of, and this runs every frame forever. */
        int n = r->bone_count > 0 ? r->bone_count : TP_MAX_BONES;
        glBindBuffer(GL_UNIFORM_BUFFER, r->bone_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0,
                        (GLsizeiptr)((size_t)n * sizeof(tp_mat4)), &r->bones);
        r->bones_dirty = false;
    }
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void tp_render_begin(tp_render *r, tp_vec4 clear_color, bool clear_depth)
{
    TP_UNUSED(r);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    /* Always clear both, always with glClear rather than a full-screen quad:
     * on a tiler a clear is free — it tells the driver the tile does not need
     * loading from memory at all. Skipping it to "save time" costs bandwidth. */
    GLbitfield bits = GL_COLOR_BUFFER_BIT;
    if (clear_depth) bits |= GL_DEPTH_BUFFER_BIT;
    glClear(bits);
}

/* ---- GPU timing ------------------------------------------------------ */

void tp_gpu_timer_init(tp_gpu_timer *t, const tp_caps *caps)
{
    memset(t, 0, sizeof(*t));
    t->last_ms = -1.0;

    if (!caps->has_timer_query) {
        TP_INFO("GL_EXT_disjoint_timer_query absent — GPU frame timing "
                "unavailable, CPU frame times only");
        return;
    }

    /* The EXT entry points are not in the GLES 3.0 core header, so they come
     * through eglGetProcAddress. Absent function pointers with a present
     * extension string is a real driver failure mode; check before use. */
    glGenQueries(2, t->query);
    if (!tp_gl_check("tp_gpu_timer_init")) return;

    t->available = true;
    TP_DEBUG("GPU timer queries available");
}

void tp_gpu_timer_shutdown(tp_gpu_timer *t)
{
    if (t->available) glDeleteQueries(2, t->query);
    memset(t, 0, sizeof(*t));
}

void tp_gpu_timer_begin(tp_gpu_timer *t)
{
    if (!t->available) return;
    glBeginQuery(GL_TIME_ELAPSED_EXT, t->query[t->slot]);
}

void tp_gpu_timer_end(tp_gpu_timer *t)
{
    if (!t->available) return;
    glEndQuery(GL_TIME_ELAPSED_EXT);
    t->pending[t->slot] = true;

    /* Read back the *other* slot — the one issued last frame. Querying the
     * result of the query we just ended would stall the pipeline, which is
     * exactly what a profiler must not do. */
    int other = t->slot ^ 1;
    if (t->pending[other]) {
        GLuint ready = 0;
        glGetQueryObjectuiv(t->query[other], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 ns = 0;
            glGetQueryObjectui64vEXT(t->query[other], GL_QUERY_RESULT, &ns);
            t->last_ms = (double)ns / 1.0e6;
            t->pending[other] = false;
        }
    }
    t->slot = other;
}

double tp_gpu_timer_last_ms(const tp_gpu_timer *t)
{
    return t->available ? t->last_ms : -1.0;
}
