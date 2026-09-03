/* flat.frag — grid lines with a distance fade.
 *
 * The fade is not decoration: without it the grid aliases badly at the horizon
 * and, on a tiler, that aliasing is fragment work spent on noise. Fading to
 * the clear colour lets the far tiles finish early.
 */

layout(std140) uniform Camera {
    mat4 u_view_proj;
    vec4 u_camera_pos;
    vec4 u_light_dir;
};

uniform vec4  u_base_color;
uniform vec4  u_line_color;
uniform float u_grid_spacing;
uniform float u_fade_distance;

in vec2 v_uv;
in vec3 v_world_pos;

out vec4 o_color;

void main()
{
    highp vec2 coord = v_world_pos.xz / u_grid_spacing;
    highp vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = 1.0 - min(min(grid.x, grid.y), 1.0);

    highp float dist = length(u_camera_pos.xz - v_world_pos.xz);
    float fade = 1.0 - clamp(dist / u_fade_distance, 0.0, 1.0);

    vec4 c = mix(u_base_color, u_line_color, line);
    o_color = vec4(c.rgb, c.a * fade);
}
