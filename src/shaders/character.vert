/* character.vert — skinned character vertex stage.
 *
 * No #version line: tp_shader.c prepends "#version 300 es" plus the precision
 * qualifiers for every program, so a shader physically cannot ask for 3.10.
 *
 * Skinning happens here, on the GPU, in the vertex shader, with the bone
 * palette in a UBO. PLAN.md §7: the A53 cores are weak and you want them free,
 * and SSBOs and compute are exactly where Panfrost's bugs concentrate, so the
 * well-trodden path is UBO + vertex-shader skinning and nothing else.
 */

layout(location = 0) in vec3  a_position;
layout(location = 1) in vec4  a_normal;    /* snorm8, w unused              */
layout(location = 2) in vec4  a_tangent;   /* snorm8, w = handedness        */
layout(location = 3) in vec2  a_uv;        /* unorm16                       */
layout(location = 4) in uvec4 a_joints;    /* u8 indices                    */
layout(location = 5) in vec4  a_weights;   /* unorm8, sums to 1             */

layout(std140) uniform Camera {
    mat4 u_view_proj;
    vec4 u_camera_pos;   /* xyz = eye, w unused                            */
    vec4 u_light_dir;    /* xyz = direction TO the light, normalised       */
};

/* 64 mat4 = 4 KiB, inside the 16 KiB every GLES 3.0 implementation must
 * provide. tp_caps_require_baseline checks this at startup rather than
 * letting the link fail with a driver-specific message. */
layout(std140) uniform Bones {
    mat4 u_bones[64];
};

uniform mat4  u_model;
uniform mat4  u_normal_matrix;
uniform float u_skinned;   /* 0 = rigid, 1 = apply the palette             */

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;

void main()
{
    vec4 pos = vec4(a_position, 1.0);
    vec3 nrm = a_normal.xyz;

    if (u_skinned > 0.5) {
        /* Weighted sum of matrices, not of transformed positions: one matrix
         * build, then one transform. Four influences is the fixed width the
         * vertex layout carries. */
        mat4 skin =
            u_bones[a_joints.x] * a_weights.x +
            u_bones[a_joints.y] * a_weights.y +
            u_bones[a_joints.z] * a_weights.z +
            u_bones[a_joints.w] * a_weights.w;

        pos = skin * pos;
        /* The export checklist forbids non-uniform bone scale, so the upper
         * 3x3 is a rotation times a uniform scale and normalising afterwards
         * is sufficient — no inverse transpose needed per bone. */
        nrm = mat3(skin) * nrm;
    }

    vec4 world = u_model * pos;
    v_world_pos = world.xyz;
    v_normal = normalize(mat3(u_normal_matrix) * nrm);
    v_uv = a_uv;

    gl_Position = u_view_proj * world;
}
