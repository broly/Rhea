#version 450

#include "definitions.glsl"
#include "pbr_helpers.glsl"

// ================== INPUTS ==================
layout(location = VERT_LOCATION_POSTION) in vec3 v_world_pos;
layout(location = VERT_LOCATION_NORMAL) in vec3 v_world_normal;
layout(location = VERT_LOCATION_UV) in vec2 v_uv;
layout(location = VERT_LOCATION_TANGENT) in vec3 v_world_tangent;

// ================== OUTPUT ==================
layout(location = 0) out vec4 out_color;

// ================== MATERIAL ==================
layout(set = SET_MATERIAL, binding = BINDING_MATERIAL_UBO) uniform MaterialUBO
{
    float base_color_mult;
    float emissive_mult;
    float occlusion_mult;
    float roughness_mult;
    float metallic_mult;
} material;

layout(set = SET_MATERIAL, binding = BINDING_BASE_COLOR) uniform sampler2D u_base_color;
layout(set = SET_MATERIAL, binding = BINDING_EMISSIVE) uniform sampler2D u_emissive;
layout(set = SET_MATERIAL, binding = BINDING_NORMAL_MAP) uniform sampler2D u_normal_map;
layout(set = SET_MATERIAL, binding = BINDING_ORM) uniform sampler2D u_orm;

// ================== CAMERA ==================
layout(set = SET_CAMERA, binding = BINDING_CAMERA_UBO) uniform CameraUBO
{
    mat4 view_proj;
    vec4 camera_pos;
} camera;

// ================== LIGHT ==================
struct Light
{
    vec4 position;
    vec4 color;
};

layout(set = SET_LIGHT, binding = BINDING_LIGHT_UBO) uniform LightUBO
{
    Light lights[8];
    int light_count;
} light_ubo;

// ================== MAIN ==================
void main()
{
    // ----- Textures (linear space) -----
    vec3 albedo = pow(texture(u_base_color, v_uv).rgb, vec3(2.2))
    * material.base_color_mult;

    vec3 emissive = texture(u_emissive, v_uv).rgb
    * material.emissive_mult;

    vec3 orm = texture(u_orm, v_uv).rgb;
    float ao        = orm.r * material.occlusion_mult;
    float roughness = orm.g * material.roughness_mult;
    float metallic  = orm.b * material.metallic_mult;

    // ----- TBN -----
    vec3 N = normalize(v_world_normal);
    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    vec3 normal_ts = texture(u_normal_map, v_uv).rgb * 2.0 - 1.0;
    normal_ts.z = 1.0 - normal_ts.z;
    vec3 normal = normalize(TBN * normal_ts);

    // ----- View -----
    vec3 V = normalize(camera.camera_pos.xyz - v_world_pos);

    // ----- Fresnel base -----
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - v_world_pos);
        vec3 H = normalize(V + L);

        float dist = length(Lpos - v_world_pos);
        float attenuation = 1.0 / (dist * dist + 1.0);
        vec3 radiance = Lcol * attenuation;

        float NDF = DistributionGGX(normal, H, roughness);
        float G   = GeometrySmith(normal, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular =
        (NDF * G * F) /
        max(4.0 * max(dot(normal, V), 0.0)
            * max(dot(normal, L), 0.0), 0.001);

        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        float NdotL = max(dot(normal, L), 0.0);
        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.01) * albedo * ao;
    vec3 hdr_color = ambient + Lo + emissive;

    out_color = vec4(hdr_color, 1.0);
}
