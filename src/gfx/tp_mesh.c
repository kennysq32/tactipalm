/* tp_mesh.c — vertex/index buffers, VAOs, and a few built-in primitives. */
#include "tp_mesh.h"
#include "tp_gl.h"

#include <stdio.h>
#include <string.h>

void tp_mesh_compute_bounds(tp_mesh *m, const tp_vertex *v, int count)
{
    if (count <= 0) {
        m->bounds_min = m->bounds_max = tp_v3_zero();
        return;
    }
    tp_vec3 lo = tp_v3(v[0].position[0], v[0].position[1], v[0].position[2]);
    tp_vec3 hi = lo;
    for (int i = 1; i < count; ++i) {
        f32 x = v[i].position[0], y = v[i].position[1], z = v[i].position[2];
        lo.x = TP_MIN(lo.x, x); lo.y = TP_MIN(lo.y, y); lo.z = TP_MIN(lo.z, z);
        hi.x = TP_MAX(hi.x, x); hi.y = TP_MAX(hi.y, y); hi.z = TP_MAX(hi.z, z);
    }
    m->bounds_min = lo;
    m->bounds_max = hi;
}

tp_result tp_mesh_create(tp_mesh *m, const char *name,
                         const tp_vertex *vertices, int vertex_count,
                         const u16 *indices, int index_count)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name ? name : "mesh");
    m->primitive = GL_TRIANGLES;

    if (!vertices || vertex_count <= 0)
        return TP_FAIL(TP_ERR_ARGS, "mesh '%s': no vertex data", m->name);
    if (vertex_count > 65536 && indices)
        return TP_FAIL(TP_ERR_ARGS,
                       "mesh '%s': %d vertices exceeds what u16 indices can "
                       "address; split the mesh", m->name, vertex_count);

    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);

    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)vertex_count * sizeof(tp_vertex)),
                 vertices, GL_STATIC_DRAW);

    const GLsizei S = TP_VERTEX_STRIDE;
    glEnableVertexAttribArray(TP_ATTR_POSITION);
    glVertexAttribPointer(TP_ATTR_POSITION, 3, GL_FLOAT, GL_FALSE, S,
                          (const void *)(size_t)offsetof(tp_vertex, position));

    glEnableVertexAttribArray(TP_ATTR_NORMAL);
    glVertexAttribPointer(TP_ATTR_NORMAL, 4, GL_BYTE, GL_TRUE, S,
                          (const void *)(size_t)offsetof(tp_vertex, normal));

    glEnableVertexAttribArray(TP_ATTR_TANGENT);
    glVertexAttribPointer(TP_ATTR_TANGENT, 4, GL_BYTE, GL_TRUE, S,
                          (const void *)(size_t)offsetof(tp_vertex, tangent));

    glEnableVertexAttribArray(TP_ATTR_UV);
    glVertexAttribPointer(TP_ATTR_UV, 2, GL_UNSIGNED_SHORT, GL_TRUE, S,
                          (const void *)(size_t)offsetof(tp_vertex, uv));

    /* Joint indices are integers, not normalised floats — glVertexAttribIPointer
     * is the GLES 3.0 entry point for that and using the float one here would
     * silently give the shader garbage in the high indices. */
    glEnableVertexAttribArray(TP_ATTR_JOINTS);
    glVertexAttribIPointer(TP_ATTR_JOINTS, 4, GL_UNSIGNED_BYTE, S,
                           (const void *)(size_t)offsetof(tp_vertex, joints));

    glEnableVertexAttribArray(TP_ATTR_WEIGHTS);
    glVertexAttribPointer(TP_ATTR_WEIGHTS, 4, GL_UNSIGNED_BYTE, GL_TRUE, S,
                          (const void *)(size_t)offsetof(tp_vertex, weights));

    if (indices && index_count > 0) {
        glGenBuffers(1, &m->ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     (GLsizeiptr)((size_t)index_count * sizeof(u16)),
                     indices, GL_STATIC_DRAW);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    /* The element buffer binding is VAO state, so it must be unbound only
     * after the VAO itself — otherwise this clears it from the VAO. */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    m->vertex_count = vertex_count;
    m->index_count  = indices ? index_count : 0;
    tp_mesh_compute_bounds(m, vertices, vertex_count);

    if (!tp_gl_check("tp_mesh_create")) {
        tp_mesh_destroy(m);
        return TP_ERR_GL;
    }

    size_t bytes = (size_t)vertex_count * sizeof(tp_vertex) +
                   (size_t)m->index_count * sizeof(u16);
    TP_DEBUG("mesh '%s': %d verts, %d indices, %zu KiB on GPU",
             m->name, vertex_count, m->index_count, (bytes + 1023) / 1024);
    return TP_OK;
}

void tp_mesh_destroy(tp_mesh *m)
{
    if (m->ibo) glDeleteBuffers(1, &m->ibo);
    if (m->vbo) glDeleteBuffers(1, &m->vbo);
    if (m->vao) glDeleteVertexArrays(1, &m->vao);
    memset(m, 0, sizeof(*m));
}

void tp_mesh_draw(const tp_mesh *m)
{
    if (!m->vao) return;
    glBindVertexArray(m->vao);
    if (m->index_count > 0)
        glDrawElements(m->primitive, m->index_count, GL_UNSIGNED_SHORT, NULL);
    else
        glDrawArrays(m->primitive, 0, m->vertex_count);
}

/* ---- primitives ------------------------------------------------------ */

static void fill_vertex(tp_vertex *v, tp_vec3 p, tp_vec3 n, tp_vec3 t, f32 u, f32 w)
{
    v->position[0] = p.x; v->position[1] = p.y; v->position[2] = p.z;
    tp_vertex_set_normal(v, n);
    tp_vertex_set_tangent(v, t, 1.0f);
    tp_vertex_set_uv(v, u, w);
    tp_vertex_set_rigid(v);
}

tp_result tp_mesh_make_cube(tp_mesh *m, f32 size)
{
    const f32 h = size * 0.5f;

    /* Faces cannot share vertices: each corner needs three different normals
     * and three different UVs, so 24 vertices rather than 8. */
    static const f32 face_normals[6][3] = {
        { 0, 0, 1}, { 0, 0,-1}, { 1, 0, 0},
        {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}
    };
    static const f32 face_tangents[6][3] = {
        { 1, 0, 0}, {-1, 0, 0}, { 0, 0,-1},
        { 0, 0, 1}, { 1, 0, 0}, { 1, 0, 0}
    };
    /* Per face: the four corners in CCW order viewed from outside. */
    static const f32 corners[6][4][3] = {
        {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}}, /* +Z */
        {{ 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}}, /* -Z */
        {{ 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}}, /* +X */
        {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}}, /* -X */
        {{-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1}}, /* +Y */
        {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}}  /* -Y */
    };
    static const f32 uvs[4][2] = {{0,0}, {1,0}, {1,1}, {0,1}};

    tp_vertex verts[24];
    u16 idx[36];

    for (int f = 0; f < 6; ++f) {
        tp_vec3 n = tp_v3(face_normals[f][0], face_normals[f][1], face_normals[f][2]);
        tp_vec3 t = tp_v3(face_tangents[f][0], face_tangents[f][1], face_tangents[f][2]);
        for (int c = 0; c < 4; ++c) {
            tp_vec3 p = tp_v3(corners[f][c][0] * h,
                              corners[f][c][1] * h,
                              corners[f][c][2] * h);
            fill_vertex(&verts[f * 4 + c], p, n, t, uvs[c][0], uvs[c][1]);
        }
        u16 base = (u16)(f * 4);
        idx[f * 6 + 0] = base;     idx[f * 6 + 1] = (u16)(base + 1);
        idx[f * 6 + 2] = (u16)(base + 2);
        idx[f * 6 + 3] = base;     idx[f * 6 + 4] = (u16)(base + 2);
        idx[f * 6 + 5] = (u16)(base + 3);
    }

    return tp_mesh_create(m, "cube", verts, 24, idx, 36);
}

tp_result tp_mesh_make_uv_sphere(tp_mesh *m, f32 radius, int rings, int sectors)
{
    rings   = TP_CLAMP(rings, 3, 128);
    sectors = TP_CLAMP(sectors, 3, 128);

    const int vcount = (rings + 1) * (sectors + 1);
    const int icount = rings * sectors * 6;
    if (vcount > 65536)
        return TP_FAIL(TP_ERR_ARGS, "sphere too dense for u16 indices");

    tp_vertex *verts = (tp_vertex *)tp_alloc((size_t)vcount * sizeof(tp_vertex));
    u16 *idx = (u16 *)tp_alloc((size_t)icount * sizeof(u16));
    if (!verts || !idx) { tp_free(verts); tp_free(idx); return TP_ERR_NOMEM; }

    int vi = 0;
    for (int r = 0; r <= rings; ++r) {
        f32 v = (f32)r / (f32)rings;
        f32 phi = v * TP_PI;
        f32 sp = sinf(phi), cp = cosf(phi);
        for (int s = 0; s <= sectors; ++s) {
            f32 u = (f32)s / (f32)sectors;
            f32 theta = u * TP_TAU;
            f32 st = sinf(theta), ct = cosf(theta);

            tp_vec3 n = tp_v3(sp * ct, cp, sp * st);
            tp_vec3 t = tp_v3(-st, 0.0f, ct);
            fill_vertex(&verts[vi++], tp_v3_scale(n, radius), n, t, u, 1.0f - v);
        }
    }

    int ii = 0;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            u16 a = (u16)(r * (sectors + 1) + s);
            u16 b = (u16)(a + sectors + 1);
            idx[ii++] = a;  idx[ii++] = b;            idx[ii++] = (u16)(a + 1);
            idx[ii++] = b;  idx[ii++] = (u16)(b + 1); idx[ii++] = (u16)(a + 1);
        }
    }

    tp_result res = tp_mesh_create(m, "sphere", verts, vi, idx, ii);
    tp_free(verts);
    tp_free(idx);
    return res;
}

tp_result tp_mesh_make_grid(tp_mesh *m, f32 extent, int divisions)
{
    divisions = TP_CLAMP(divisions, 1, 200);
    const int n = divisions + 1;
    const int vcount = n * n;
    const int icount = divisions * divisions * 6;

    tp_vertex *verts = (tp_vertex *)tp_alloc((size_t)vcount * sizeof(tp_vertex));
    u16 *idx = (u16 *)tp_alloc((size_t)icount * sizeof(u16));
    if (!verts || !idx) { tp_free(verts); tp_free(idx); return TP_ERR_NOMEM; }

    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            f32 fx = (f32)x / (f32)divisions;
            f32 fz = (f32)z / (f32)divisions;
            tp_vec3 p = tp_v3((fx - 0.5f) * extent, 0.0f, (fz - 0.5f) * extent);
            fill_vertex(&verts[z * n + x], p, tp_v3_up(), tp_v3(1, 0, 0), fx, fz);
        }
    }

    int ii = 0;
    for (int z = 0; z < divisions; ++z) {
        for (int x = 0; x < divisions; ++x) {
            u16 a = (u16)(z * n + x);
            u16 b = (u16)(a + n);
            idx[ii++] = a; idx[ii++] = b;            idx[ii++] = (u16)(a + 1);
            idx[ii++] = b; idx[ii++] = (u16)(b + 1); idx[ii++] = (u16)(a + 1);
        }
    }

    tp_result res = tp_mesh_create(m, "grid", verts, vcount, idx, ii);
    tp_free(verts);
    tp_free(idx);
    return res;
}
