/* tp_math.h — small float math library: vec2/3/4, quat, mat4.
 *
 * Header-only, static inline, plain scalar f32. Deliberately no intrinsics:
 * the target baseline is ARMv8.0-A (Cortex-A53) so there is no fp16 and no
 * dotprod to reach for anyway, and plain C lets the compiler autovectorise
 * with baseline NEON on aarch64 and SSE2 on the dev box from one source.
 *
 * Conventions, fixed once so future-you cannot argue with present-you:
 *   - Column-major mat4, same as GL. m[col][row] is the natural index, and
 *     `.e[16]` is directly uploadable with transpose = GL_FALSE.
 *   - Right-handed world space, +Y up, -Z forward (glTF convention).
 *   - Quaternions are (x, y, z, w) with w last — again matching glTF, so the
 *     asset converter never has to reorder components.
 *   - Angles in radians. Distances in metres.
 */
#ifndef TP_MATH_H
#define TP_MATH_H

#include "tp_common.h"
#include <math.h>

#define TP_PI   3.14159265358979323846f
#define TP_TAU  6.28318530717958647692f
#define TP_DEG2RAD (TP_PI / 180.0f)
#define TP_RAD2DEG (180.0f / TP_PI)
#define TP_EPSILON 1e-6f

/* ---- vectors --------------------------------------------------------- */
typedef struct { f32 x, y; }       tp_vec2;
typedef struct { f32 x, y, z; }    tp_vec3;
typedef struct { f32 x, y, z, w; } tp_vec4;
typedef tp_vec4 tp_quat; /* (x,y,z,w), w scalar last */

static inline tp_vec2 tp_v2(f32 x, f32 y)             { return (tp_vec2){x, y}; }
static inline tp_vec3 tp_v3(f32 x, f32 y, f32 z)      { return (tp_vec3){x, y, z}; }
static inline tp_vec4 tp_v4(f32 x, f32 y, f32 z, f32 w){ return (tp_vec4){x, y, z, w}; }
static inline tp_vec3 tp_v3_splat(f32 s)              { return (tp_vec3){s, s, s}; }
static inline tp_vec3 tp_v3_zero(void)                { return (tp_vec3){0, 0, 0}; }
static inline tp_vec3 tp_v3_one(void)                 { return (tp_vec3){1, 1, 1}; }
static inline tp_vec3 tp_v3_up(void)                  { return (tp_vec3){0, 1, 0}; }
/* glTF/OpenGL forward is -Z. */
static inline tp_vec3 tp_v3_forward(void)             { return (tp_vec3){0, 0, -1}; }

static inline tp_vec3 tp_v3_add(tp_vec3 a, tp_vec3 b)
    { return (tp_vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline tp_vec3 tp_v3_sub(tp_vec3 a, tp_vec3 b)
    { return (tp_vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline tp_vec3 tp_v3_mul(tp_vec3 a, tp_vec3 b)
    { return (tp_vec3){a.x * b.x, a.y * b.y, a.z * b.z}; }
static inline tp_vec3 tp_v3_scale(tp_vec3 a, f32 s)
    { return (tp_vec3){a.x * s, a.y * s, a.z * s}; }
static inline tp_vec3 tp_v3_neg(tp_vec3 a)
    { return (tp_vec3){-a.x, -a.y, -a.z}; }
static inline f32 tp_v3_dot(tp_vec3 a, tp_vec3 b)
    { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline tp_vec3 tp_v3_cross(tp_vec3 a, tp_vec3 b)
    { return (tp_vec3){a.y * b.z - a.z * b.y,
                       a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x}; }
static inline f32 tp_v3_len_sq(tp_vec3 a) { return tp_v3_dot(a, a); }
static inline f32 tp_v3_len(tp_vec3 a)    { return sqrtf(tp_v3_dot(a, a)); }

static inline tp_vec3 tp_v3_norm(tp_vec3 a)
{
    f32 l2 = tp_v3_len_sq(a);
    if (l2 < TP_EPSILON) return tp_v3_zero();
    f32 inv = 1.0f / sqrtf(l2);
    return (tp_vec3){a.x * inv, a.y * inv, a.z * inv};
}

static inline tp_vec3 tp_v3_lerp(tp_vec3 a, tp_vec3 b, f32 t)
    { return (tp_vec3){a.x + (b.x - a.x) * t,
                       a.y + (b.y - a.y) * t,
                       a.z + (b.z - a.z) * t}; }

static inline f32 tp_lerpf(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

/* ---- quaternions ----------------------------------------------------- */
static inline tp_quat tp_quat_identity(void) { return (tp_quat){0, 0, 0, 1}; }

static inline tp_quat tp_quat_axis_angle(tp_vec3 axis, f32 radians)
{
    tp_vec3 n = tp_v3_norm(axis);
    f32 h = radians * 0.5f;
    f32 s = sinf(h);
    return (tp_quat){n.x * s, n.y * s, n.z * s, cosf(h)};
}

/* Hamilton product: applying `a` after `b`. */
static inline tp_quat tp_quat_mul(tp_quat a, tp_quat b)
{
    return (tp_quat){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

static inline f32 tp_quat_dot(tp_quat a, tp_quat b)
    { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

static inline tp_quat tp_quat_norm(tp_quat q)
{
    f32 l2 = tp_quat_dot(q, q);
    if (l2 < TP_EPSILON) return tp_quat_identity();
    f32 inv = 1.0f / sqrtf(l2);
    return (tp_quat){q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

static inline tp_quat tp_quat_conj(tp_quat q)
    { return (tp_quat){-q.x, -q.y, -q.z, q.w}; }

static inline tp_vec3 tp_quat_rotate(tp_quat q, tp_vec3 v)
{
    /* v + 2w(q x v) + 2(q x (q x v)) — cheaper than building a matrix. */
    tp_vec3 u = tp_v3(q.x, q.y, q.z);
    tp_vec3 t = tp_v3_scale(tp_v3_cross(u, v), 2.0f);
    return tp_v3_add(tp_v3_add(v, tp_v3_scale(t, q.w)), tp_v3_cross(u, t));
}

/* Shortest-arc spherical interpolation. Falls back to nlerp when the inputs
 * are nearly parallel, where slerp is numerically unstable and visually
 * indistinguishable anyway. This is the animation hot path. */
static inline tp_quat tp_quat_slerp(tp_quat a, tp_quat b, f32 t)
{
    f32 d = tp_quat_dot(a, b);
    if (d < 0.0f) { b = (tp_quat){-b.x, -b.y, -b.z, -b.w}; d = -d; }

    if (d > 0.9995f) {
        tp_quat r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                     a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        return tp_quat_norm(r);
    }

    f32 theta = acosf(TP_CLAMP(d, -1.0f, 1.0f));
    f32 sin_theta = sinf(theta);
    f32 wa = sinf((1.0f - t) * theta) / sin_theta;
    f32 wb = sinf(t * theta) / sin_theta;
    return (tp_quat){a.x * wa + b.x * wb, a.y * wa + b.y * wb,
                     a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

/* ---- mat4 ------------------------------------------------------------ */
/* Column-major: e[col * 4 + row], uploadable with transpose = GL_FALSE. */
typedef struct { f32 e[16]; } tp_mat4;

static inline tp_mat4 tp_mat4_identity(void)
{
    tp_mat4 m = {{0}};
    m.e[0] = m.e[5] = m.e[10] = m.e[15] = 1.0f;
    return m;
}

static inline tp_mat4 tp_mat4_mul(tp_mat4 a, tp_mat4 b)
{
    tp_mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.e[c * 4 + row] = a.e[0 * 4 + row] * b.e[c * 4 + 0] +
                               a.e[1 * 4 + row] * b.e[c * 4 + 1] +
                               a.e[2 * 4 + row] * b.e[c * 4 + 2] +
                               a.e[3 * 4 + row] * b.e[c * 4 + 3];
        }
    }
    return r;
}

static inline tp_vec4 tp_mat4_mul_v4(tp_mat4 m, tp_vec4 v)
{
    return (tp_vec4){
        m.e[0] * v.x + m.e[4] * v.y + m.e[8]  * v.z + m.e[12] * v.w,
        m.e[1] * v.x + m.e[5] * v.y + m.e[9]  * v.z + m.e[13] * v.w,
        m.e[2] * v.x + m.e[6] * v.y + m.e[10] * v.z + m.e[14] * v.w,
        m.e[3] * v.x + m.e[7] * v.y + m.e[11] * v.z + m.e[15] * v.w
    };
}

static inline tp_mat4 tp_mat4_translate(tp_vec3 t)
{
    tp_mat4 m = tp_mat4_identity();
    m.e[12] = t.x; m.e[13] = t.y; m.e[14] = t.z;
    return m;
}

static inline tp_mat4 tp_mat4_scale(tp_vec3 s)
{
    tp_mat4 m = {{0}};
    m.e[0] = s.x; m.e[5] = s.y; m.e[10] = s.z; m.e[15] = 1.0f;
    return m;
}

static inline tp_mat4 tp_mat4_from_quat(tp_quat q)
{
    f32 x = q.x, y = q.y, z = q.z, w = q.w;
    f32 xx = x * x, yy = y * y, zz = z * z;
    f32 xy = x * y, xz = x * z, yz = y * z;
    f32 wx = w * x, wy = w * y, wz = w * z;

    tp_mat4 m = tp_mat4_identity();
    m.e[0]  = 1.0f - 2.0f * (yy + zz);
    m.e[1]  =        2.0f * (xy + wz);
    m.e[2]  =        2.0f * (xz - wy);
    m.e[4]  =        2.0f * (xy - wz);
    m.e[5]  = 1.0f - 2.0f * (xx + zz);
    m.e[6]  =        2.0f * (yz + wx);
    m.e[8]  =        2.0f * (xz + wy);
    m.e[9]  =        2.0f * (yz - wx);
    m.e[10] = 1.0f - 2.0f * (xx + yy);
    return m;
}

/* Compose translation * rotation * scale directly. Used per bone per frame,
 * so it avoids the three matrix multiplies the naive version would do. */
static inline tp_mat4 tp_mat4_trs(tp_vec3 t, tp_quat r, tp_vec3 s)
{
    tp_mat4 m = tp_mat4_from_quat(r);
    m.e[0] *= s.x; m.e[1] *= s.x; m.e[2]  *= s.x;
    m.e[4] *= s.y; m.e[5] *= s.y; m.e[6]  *= s.y;
    m.e[8] *= s.z; m.e[9] *= s.z; m.e[10] *= s.z;
    m.e[12] = t.x; m.e[13] = t.y; m.e[14] = t.z;
    return m;
}

/* Right-handed, depth range [-1, 1] (GL/GLES default glDepthRangef). */
static inline tp_mat4 tp_mat4_perspective(f32 fovy_rad, f32 aspect,
                                          f32 znear, f32 zfar)
{
    f32 f = 1.0f / tanf(fovy_rad * 0.5f);
    tp_mat4 m = {{0}};
    m.e[0]  = f / aspect;
    m.e[5]  = f;
    m.e[10] = (zfar + znear) / (znear - zfar);
    m.e[11] = -1.0f;
    m.e[14] = (2.0f * zfar * znear) / (znear - zfar);
    return m;
}

static inline tp_mat4 tp_mat4_ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f)
{
    tp_mat4 m = tp_mat4_identity();
    m.e[0]  =  2.0f / (r - l);
    m.e[5]  =  2.0f / (t - b);
    m.e[10] = -2.0f / (f - n);
    m.e[12] = -(r + l) / (r - l);
    m.e[13] = -(t + b) / (t - b);
    m.e[14] = -(f + n) / (f - n);
    return m;
}

static inline tp_mat4 tp_mat4_look_at(tp_vec3 eye, tp_vec3 target, tp_vec3 up)
{
    tp_vec3 fwd = tp_v3_norm(tp_v3_sub(target, eye));
    tp_vec3 side = tp_v3_norm(tp_v3_cross(fwd, up));
    tp_vec3 u = tp_v3_cross(side, fwd);

    tp_mat4 m = tp_mat4_identity();
    m.e[0] = side.x; m.e[4] = side.y; m.e[8]  = side.z;
    m.e[1] = u.x;    m.e[5] = u.y;    m.e[9]  = u.z;
    m.e[2] = -fwd.x; m.e[6] = -fwd.y; m.e[10] = -fwd.z;
    m.e[12] = -tp_v3_dot(side, eye);
    m.e[13] = -tp_v3_dot(u, eye);
    m.e[14] =  tp_v3_dot(fwd, eye);
    return m;
}

/* Inverse of a rigid-plus-uniform-scale transform. Correct for anything the
 * animation system produces; do not feed it a sheared or non-uniformly scaled
 * matrix. (The asset export checklist forbids non-uniform bone scale for
 * exactly this reason.) */
static inline tp_mat4 tp_mat4_inverse_rigid(tp_mat4 m)
{
    f32 sx2 = m.e[0] * m.e[0] + m.e[1] * m.e[1] + m.e[2]  * m.e[2];
    f32 inv_s = (sx2 > TP_EPSILON) ? 1.0f / sx2 : 1.0f;

    tp_mat4 r = tp_mat4_identity();
    r.e[0] = m.e[0] * inv_s; r.e[4] = m.e[1] * inv_s; r.e[8]  = m.e[2]  * inv_s;
    r.e[1] = m.e[4] * inv_s; r.e[5] = m.e[5] * inv_s; r.e[9]  = m.e[6]  * inv_s;
    r.e[2] = m.e[8] * inv_s; r.e[6] = m.e[9] * inv_s; r.e[10] = m.e[10] * inv_s;

    tp_vec3 t = tp_v3(m.e[12], m.e[13], m.e[14]);
    r.e[12] = -(r.e[0] * t.x + r.e[4] * t.y + r.e[8]  * t.z);
    r.e[13] = -(r.e[1] * t.x + r.e[5] * t.y + r.e[9]  * t.z);
    r.e[14] = -(r.e[2] * t.x + r.e[6] * t.y + r.e[10] * t.z);
    return r;
}

/* Normal matrix for a rigid transform is just its rotation part; returning
 * mat3-as-mat4 keeps the uniform upload path uniform. */
static inline tp_mat4 tp_mat4_normal_matrix(tp_mat4 model)
{
    tp_mat4 r = model;
    r.e[12] = r.e[13] = r.e[14] = 0.0f;
    r.e[3] = r.e[7] = r.e[11] = 0.0f;
    r.e[15] = 1.0f;
    return r;
}

#endif /* TP_MATH_H */
