#version 450

// ---------- Inputs ----------
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_world_pos;
layout(location = 2) in vec3 v_world_normal;

// ---------- Output ----------
layout(location = 0) out vec4 out_color;

// ---------- Material (set = 1) ----------
layout(set = 1, binding = 0) uniform MaterialUBO
{
    vec4  base_color;
    float metallic;
    float roughness;
} material;

layout(set = 1, binding = 1) uniform sampler2D u_albedo;

// ---------- Light (set = 2) ----------
struct Light
{
    vec3 position;
    float pad0;
    vec3 color;
    float pad1;
};

layout(set = 2, binding = 0) uniform LightUBO
{
    Light lights[8];
    int light_count;
} light_ubo;

// ---------- Main ----------
void main()
{
    
    //vec3 albedo = texture(u_albedo, v_uv).rgb * material.base_color.rgb;

    // if (any(isnan(albedo)))
            
    vec3 albedo = vec3(1.0);
    vec3 N = normalize(v_world_normal);
    vec3 L = normalize(vec3(0.3, 1.0, 0.3));

    float NdotL = max(dot(N, L), 0.0);

    out_color = vec4(albedo * (0.2 + NdotL), 1.0);
}
