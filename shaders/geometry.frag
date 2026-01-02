#version 450

// ---------- Inputs ----------
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;

// ---------- Output ----------
layout(location = 0) out vec4 out_color;

// ---------- Material (set = 1) ----------
layout(set = 1, binding = 0) uniform MaterialUBO
{
    vec4 base_color;
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D u_albedo;

// ---------- Light (set = 2) ----------
struct Light
{
    vec4 position; // xyz
    vec4 color;    // rgb
};

layout(set = 2, binding = 0) uniform LightUBO
{
    Light lights[8];
    int light_count; // x = light_count
} light_ubo;

// ---------- Main ----------
void main()
{
    vec3 albedo =
    texture(u_albedo, v_uv).rgb *
    material.base_color.rgb;

    vec3 N = normalize(v_world_normal);

    vec3 lighting = albedo * 0.05; // ambient

    int light_count = int(light_ubo.light_count);

    for (int i = 0; i < light_count; ++i)
    {
        vec3 light_pos = light_ubo.lights[i].position.xyz;
        vec3 light_col = light_ubo.lights[i].color.rgb;

        vec3 L = light_pos - v_world_pos;
        float dist = length(L);
        L /= max(dist, 0.0001);

        float NdotL = max(dot(N, L), 0.0);

        float attenuation = 1.0 / (dist * dist + 1.0);

        lighting +=
        albedo *
        light_col *
        NdotL *
        attenuation;
    }

    out_color = vec4(lighting, 1.0);
}
