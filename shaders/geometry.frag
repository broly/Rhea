#version 450

// ---------- Inputs ----------
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;

// ---------- Output ----------
layout(location = 0) out vec4 out_color;

// ---------- Material ----------
layout(set = 1, binding = 0) uniform MaterialUBO
{
    vec4 base_color;
    vec4 params; // x = metallic, y = roughness
} material;

layout(set = 1, binding = 1) uniform sampler2D u_albedo;
layout(set = 1, binding = 2) uniform sampler2D u_normal_map;

// ---------- Light ----------
struct Light
{
    vec4 position;
    vec4 color;
};

layout(set = 2, binding = 0) uniform LightUBO
{
    Light lights[8];
    int light_count;
} light_ubo;

// ---------- Main ----------
void main()
{
    vec3 albedo = texture(u_albedo, v_uv).rgb * material.base_color.rgb;

    // ----- TBN -----
    vec3 N = normalize(v_world_normal);
    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(cross(N, T));

    mat3 TBN = mat3(T, B, N);

    // ----- Normal map -----
    vec3 normal_ts = texture(u_normal_map, v_uv).rgb;
    normal_ts = normal_ts * 2.0 - 1.0; // [0,1] → [-1,1]

    vec3 normal_ws = normalize(TBN * normal_ts);

    // ----- Lighting -----
    vec3 lighting = albedo * 0.05; // ambient

    int light_count = int(light_ubo.light_count);

    for (int i = 0; i < light_count; ++i)
    {
        vec3 light_pos = light_ubo.lights[i].position.xyz;
        vec3 light_col = light_ubo.lights[i].color.rgb;

        vec3 L = light_pos - v_world_pos;
        float dist = length(L);
        L /= max(dist, 0.0001);

        float NdotL = max(dot(normal_ws, L), 0.0);
        float attenuation = 1.0 / (dist * dist + 1.0);

        lighting += albedo * light_col * NdotL * attenuation;
    }

    out_color = vec4(lighting, 1.0);
}
