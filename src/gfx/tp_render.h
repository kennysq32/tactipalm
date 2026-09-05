/* tp_render.h — frame state and the two uniform buffers everything shares.
 *
 * Both UBOs are allocated once and updated with glBufferSubData, never
 * reallocated: PLAN.md §6 rules out per-frame allocation, and on unified
 * memory a per-frame buffer orphan is a per-frame page fault.
 *
 * UBOs, not SSBOs, and never a compute shader — CONTRACT.md §2 puts those
 * outside the target surface entirely.
 */
#ifndef TP_RENDER_H
#define TP_RENDER_H

#include "../core/tp_common.h"
#include "../core/tp_math.h"
#include "tp_caps.h"
#include "tp_mesh.h"
#include "tp_shader.h"

enum {
    TP_UBO_CAMERA = 0,
    TP_UBO_BONES  = 1
};

/* std140. Every member is vec4-aligned by construction, so the C struct and
 * the block layout agree without padding surprises. Verified by the static
 * assert below rather than trusted. */
typedef struct {
    tp_mat4 view_proj;   /*  0 .. 63 */
    tp_vec4 camera_pos;  /* 64 .. 79 */
    tp_vec4 light_dir;   /* 80 .. 95 */
} tp_camera_block;

_Static_assert(sizeof(tp_camera_block) == 96,
               "Camera UBO must match the std140 block in the shaders");

typedef struct {
    tp_mat4 bones[TP_MAX_BONES];
} tp_bone_block;

_Static_assert(sizeof(tp_bone_block) == TP_MAX_BONES * 64,
               "Bone palette UBO must be a tight array of mat4");

typedef struct {
    u32 camera_ubo;
    u32 bone_ubo;
    tp_camera_block camera;
    tp_bone_block   bones;
    int  bone_count;
    bool bones_dirty;
    bool camera_dirty;
} tp_render;

tp_result tp_render_init(tp_render *r, const tp_caps *caps);
void tp_render_shutdown(tp_render *r);

/* Bind both blocks of a program to the shared binding points. Call once per
 * program after linking. */
tp_result tp_render_bind_blocks(tp_render *r, tp_shader *sh, bool wants_bones);

void tp_render_set_camera(tp_render *r, tp_mat4 view_proj, tp_vec3 eye,
                          tp_vec3 light_dir);
void tp_render_set_bone(tp_render *r, int index, tp_mat4 m);
void tp_render_set_bone_count(tp_render *r, int count);
/* Push whatever changed. Called once per frame, before any draw. */
void tp_render_flush(tp_render *r);

/* Frame-level state the renderer sets identically every frame. Depth test on,
 * back-face culling on, blending off — the tiler-friendly default. */
void tp_render_begin(tp_render *r, tp_vec4 clear_color, bool clear_depth);

/* GPU timing via GL_EXT_disjoint_timer_query, when the driver has it.
 * CONTRACT.md §2.4 is explicit that this must be queried, not assumed, so
 * every entry point here is a no-op when the extension is absent. */
typedef struct {
    u32  query[2];
    int  slot;
    bool available;
    bool pending[2];
    double last_ms;
} tp_gpu_timer;

void tp_gpu_timer_init(tp_gpu_timer *t, const tp_caps *caps);
void tp_gpu_timer_shutdown(tp_gpu_timer *t);
void tp_gpu_timer_begin(tp_gpu_timer *t);
void tp_gpu_timer_end(tp_gpu_timer *t);
/* Milliseconds for the most recent completed frame, or -1 when unavailable. */
double tp_gpu_timer_last_ms(const tp_gpu_timer *t);

#endif /* TP_RENDER_H */
