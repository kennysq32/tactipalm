/* character.frag — stylised character shading.
 *
 * PLAN.md §7 argues against PBR + IBL on a G31: the naive PBR path is a poor
 * trade there, and a stylised look is both cheaper and more likely to read as
 * deliberate. So this is one directional light with a half-lambert wrap, a
 * ramp lookup for the shading response, and a rim term that does most of the
 * visual work for almost no cost. No shadow maps, no post, nothing that costs
 * a full-screen pass — on a tiler bandwidth is the budget.
 *
 * Default precision here is mediump (see tp_shader.c). Anything that needs
 * more says so, which keeps the cost visible where it is paid.
 */

layout(std140) uniform Camera {
    mat4 u_view_proj;
    vec4 u_camera_pos;
    vec4 u_light_dir;
};

uniform sampler2D u_albedo;
uniform sampler2D u_ramp;

uniform vec4  u_base_color;
uniform vec3  u_light_color;
uniform vec3  u_ambient;
uniform vec3  u_rim_color;
uniform float u_rim_power;
uniform float u_rim_strength;
uniform float u_use_albedo;   /* 0 or 1 — cheaper than a shader permutation */
uniform float u_use_ramp;

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;

out vec4 o_color;

void main()
{
    vec3 n = normalize(v_normal);
    vec3 l = normalize(u_light_dir.xyz);
    highp vec3 view_vec = u_camera_pos.xyz - v_world_pos;
    vec3 v = normalize(view_vec);

    vec4 albedo = u_base_color;
    if (u_use_albedo > 0.5)
        albedo *= texture(u_albedo, v_uv);

    /* Half-lambert: remap N.L from [-1,1] to [0,1] so the terminator softens
     * and the unlit side keeps shape instead of going flat black. */
    float ndl = dot(n, l) * 0.5 + 0.5;

    vec3 shade;
    if (u_use_ramp > 0.5) {
        /* One texture fetch replaces a stack of arithmetic and, more useful,
         * moves the entire lighting response into art direction. */
        shade = texture(u_ramp, vec2(ndl, 0.5)).rgb;
    } else {
        shade = vec3(ndl * ndl);
    }

    /* Fresnel-ish rim. pow() on a mediump value is fine here; the term is a
     * broad gradient, not a highlight. */
    float rim = 1.0 - max(dot(n, v), 0.0);
    rim = pow(rim, u_rim_power) * u_rim_strength;

    vec3 lit = albedo.rgb * (u_ambient + u_light_color * shade);
    lit += u_rim_color * rim;

    o_color = vec4(lit, albedo.a);
}
