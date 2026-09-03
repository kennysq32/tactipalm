/* tp_mesh.h — vertex buffers, index buffers, vertex array objects.
 *
 * One canonical vertex layout for the whole renderer, 32 bytes, interleaved,
 * matching what the offline converter will emit (PLAN.md §6: "interleaved,
 * pre-transformed vertex buffers in final GPU layout", "quantised bone
 * indices/weights"). Everything smaller than a position is quantised, because
 * on a tile-based deferred renderer vertex *bandwidth* is the cost and the
 * A53 has no headroom to unpack anything at load time.
 *
 *   offset  size  attr  type          contents
 *   ------  ----  ----  ------------  -------------------------------------
 *        0    12     0  f32 x3        position, metres
 *       12     4     1  snorm8 x4     normal (w unused, keeps alignment)
 *       16     4     2  snorm8 x4     tangent + handedness in w
 *       20     4     3  unorm16 x2    uv
 *       24     4     4  u8 x4         joint indices (<= 64 bones)
 *       28     4     5  unorm8 x4     joint weights, sum 1
 *   ------  ----
 *       32          stride
 *
 * Indices are always u16. The content budget is 20k triangles, so u16 is
 * always sufficient and halves index bandwidth against u32.
 */
#ifndef TP_MESH_H
#define TP_MESH_H

#include "../core/tp_common.h"
#include "../core/tp_math.h"

#define TP_VERTEX_STRIDE 32
#define TP_MAX_BONES     64   /* 64 * mat4 = 4 KiB, fits any GLES 3.0 UBO */

enum {
    TP_ATTR_POSITION = 0,
    TP_ATTR_NORMAL   = 1,
    TP_ATTR_TANGENT  = 2,
    TP_ATTR_UV       = 3,
    TP_ATTR_JOINTS   = 4,
    TP_ATTR_WEIGHTS  = 5,
    TP_ATTR_COUNT
};

typedef struct {
    f32 position[3];
    i8  normal[4];
    i8  tangent[4];
    u16 uv[2];
    u8  joints[4];
    u8  weights[4];
} tp_vertex;

_Static_assert(sizeof(tp_vertex) == TP_VERTEX_STRIDE,
               "tp_vertex must stay exactly 32 bytes — the converter and the "
               "VAO setup both hard-code this layout");

/* Quantisation helpers, shared with the offline converter so both sides
 * round identically. */
static inline i8 tp_pack_snorm8(f32 v)
{
    f32 c = TP_CLAMP(v, -1.0f, 1.0f);
    return (i8)(c * 127.0f + (c >= 0.0f ? 0.5f : -0.5f));
}
static inline u8 tp_pack_unorm8(f32 v)
{
    return (u8)(TP_CLAMP(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}
static inline u16 tp_pack_unorm16(f32 v)
{
    return (u16)(TP_CLAMP(v, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

static inline void tp_vertex_set_normal(tp_vertex *v, tp_vec3 n)
{
    v->normal[0] = tp_pack_snorm8(n.x);
    v->normal[1] = tp_pack_snorm8(n.y);
    v->normal[2] = tp_pack_snorm8(n.z);
    v->normal[3] = 0;
}
static inline void tp_vertex_set_tangent(tp_vertex *v, tp_vec3 t, f32 handedness)
{
    v->tangent[0] = tp_pack_snorm8(t.x);
    v->tangent[1] = tp_pack_snorm8(t.y);
    v->tangent[2] = tp_pack_snorm8(t.z);
    v->tangent[3] = tp_pack_snorm8(handedness);
}
static inline void tp_vertex_set_uv(tp_vertex *v, f32 u, f32 t)
{
    v->uv[0] = tp_pack_unorm16(u);
    v->uv[1] = tp_pack_unorm16(t);
}
/* Rigid (unskinned) geometry still needs a valid binding, or the skinning
 * vertex shader produces a zero matrix and the mesh collapses to a point. */
static inline void tp_vertex_set_rigid(tp_vertex *v)
{
    v->joints[0] = v->joints[1] = v->joints[2] = v->joints[3] = 0;
    v->weights[0] = 255;
    v->weights[1] = v->weights[2] = v->weights[3] = 0;
}

typedef struct {
    u32 vao, vbo, ibo;
    int vertex_count;
    int index_count;
    u32 primitive;      /* GL_TRIANGLES etc.                              */
    tp_vec3 bounds_min; /* object space, for camera framing and culling   */
    tp_vec3 bounds_max;
    char name[32];
} tp_mesh;

/* Upload. `indices` may be NULL for a non-indexed draw. Both buffers are
 * GL_STATIC_DRAW: PLAN.md §6 forbids per-frame allocation, and nothing in the
 * character pipeline needs a dynamic vertex buffer. */
tp_result tp_mesh_create(tp_mesh *m, const char *name,
                         const tp_vertex *vertices, int vertex_count,
                         const u16 *indices, int index_count);
void tp_mesh_destroy(tp_mesh *m);
void tp_mesh_draw(const tp_mesh *m);
/* Recompute bounds_min/max from the vertex data. Called by tp_mesh_create. */
void tp_mesh_compute_bounds(tp_mesh *m, const tp_vertex *v, int count);

/* Built-in primitives — enough to exercise the pipeline before any asset
 * converter exists. Both are unit-sized and centred on the origin. */
tp_result tp_mesh_make_cube(tp_mesh *m, f32 size);
tp_result tp_mesh_make_grid(tp_mesh *m, f32 extent, int divisions);
tp_result tp_mesh_make_uv_sphere(tp_mesh *m, f32 radius, int rings, int sectors);

#endif /* TP_MESH_H */
