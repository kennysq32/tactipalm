/* flat.vert — unlit, unskinned. Ground grid, debug geometry, anything that
 * should not pay for the character path. */

layout(location = 0) in vec3 a_position;
layout(location = 3) in vec2 a_uv;

layout(std140) uniform Camera {
    mat4 u_view_proj;
    vec4 u_camera_pos;
    vec4 u_light_dir;
};

uniform mat4 u_model;

out vec2 v_uv;
out vec3 v_world_pos;

void main()
{
    vec4 world = u_model * vec4(a_position, 1.0);
    v_world_pos = world.xyz;
    v_uv = a_uv;
    gl_Position = u_view_proj * world;
}
