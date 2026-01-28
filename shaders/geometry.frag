#version 450

#include "definitions.glsl"
#include "pbr_helpers.glsl"
#include "math.glsl"
#include "layout.glsl"

// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;

// ================== OUTPUT ==================
layout(location = 0) out vec4 out_color;

// ================== MATERIAL ==================
layout(set = SET_PBR, binding = BINDING_MATERIAL) uniform MaterialUBO
{
    float base_color_mult;
    float emissive_mult;
    float occlusion_mult;
    float roughness_mult;
    float metallic_mult;
} material_ubo;

layout(set = SET_PBR, binding = BINDING_SAMPLER_ALBEDO) uniform sampler2D u_base_color;
layout(set = SET_PBR, binding = BINDING_SAMPLER_EMISSIVE) uniform sampler2D u_emissive;
layout(set = SET_PBR, binding = BINDING_SAMPLER_NORMAL) uniform sampler2D u_normal_map;
layout(set = SET_PBR, binding = BINDING_SAMPLER_ORM) uniform sampler2D u_orm;

// ================== CAMERA ==================
layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) uniform CameraUBO
{
    mat4 view_proj;
    vec4 camera_pos;
} camera_ubo;

// ================== LIGHT ==================
struct PointLight
{
    vec4 position;
    vec4 color;
};

struct DirectionalLight
{
    mat4 light_vp;  // view-projection for shadow
    vec4 direction; // xyz normalized (world)
    vec4 color;     // rgb * intensity
};

layout(set = SET_LIGHT, binding = BINDING_UBO_LIGHT) uniform LightUBO
{
    PointLight lights[8];
    int light_count;

    DirectionalLight dir_light;
    int has_dir_light;

} light_ubo;



layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW) uniform sampler2D u_shadow_depth;


// ================== SHADOW ==================
float shadow_factor(vec3 world_pos, vec3 Ng)
{
    vec4 light_clip = light_ubo.dir_light.light_vp * vec4(world_pos, 1.0);
    vec3 proj = light_clip.xyz / light_clip.w;

    if (
        proj.x < -1.0 || proj.x > 1.0 ||
        proj.y < -1.0 || proj.y > 1.0 ||
        proj.z <  0.0 || proj.z > 1.0
    )
    return 1.0;

    vec2 uv = proj.xy * 0.5 + 0.5;
    float depth = texture(u_shadow_depth, uv).r;

    float ndotl = max(dot(Ng, -light_ubo.dir_light.direction.xyz), 0.0);
    float bias  = max(0.0005 * (1.0 - ndotl), 0.0001);

    return (proj.z - bias > depth) ? 0.0 : 1.0;
}

// ================== MAIN ==================
void main()
{
    vec4 base_tx = texture(u_base_color, v_uv);

#if BLEND_MODE_TRANSLUCENT
    float alpha = base_tx.a;
    
    if (alpha <= 0.001)
        discard;
#endif

    // ----- Albedo (linear) -----
    vec3 albedo = pow(base_tx.rgb, vec3(2.2)) * material_ubo.base_color_mult;

    // ----- ORM -----
    vec3 orm = texture(u_orm, v_uv).rgb;
    float ao        = orm.r * material_ubo.occlusion_mult;
    float roughness = orm.g * material_ubo.roughness_mult;
    float metallic  = orm.b * material_ubo.metallic_mult;

#if BLEND_MODE_TRANSLUCENT
    metallic  = 0.0;
    roughness = 1.0;
#endif

    // ----- Normals -----
    vec3 Ng = normalize(v_world_normal);
    vec3 N  = Ng;

#if !BLEND_MODE_TRANSLUCENT
    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(v_world_bitangent);
    mat3 TBN = mat3(T, B, Ng);

    vec3 n_tx = texture(u_normal_map, v_uv).rgb;
    n_tx.y = 1.0 - n_tx.y;
    vec3 n_ts = n_tx * 2.0 - 1.0;

    N = normalize(TBN * n_ts);
#endif

    // ----- View -----
    vec3 V = normalize(camera_ubo.camera_pos.xyz - v_world_pos);

    vec3 Lo = vec3(0.0);

    // ================== POINT LIGHTS ==================
    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - v_world_pos);

        float NdotL_geom = max(dot(Ng, L), 0.0);
        float NdotL_nm   = max(dot(N,  L), 0.0);
        float NdotL      = min(NdotL_geom, NdotL_nm);

        if (NdotL <= 0.0)
            continue;

        float dist = length(Lpos - v_world_pos);
        float attenuation = 1.0 / (dist * dist + 1.0);
        vec3 radiance = Lcol * attenuation;

#if BLEND_MODE_TRANSLUCENT
        Lo += albedo / PI * radiance * NdotL;
#else
        vec3 H = normalize(V + L);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 spec =
            (NDF * G * F) /
            max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);

        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);

        Lo += (kD * albedo / PI + spec) * radiance * NdotL;
#endif
    }

    // ================== DIRECTIONAL ==================
    float shadow = 1.0;

    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);

        float NdotL_geom = max(dot(Ng, L), 0.0);
        float NdotL_nm   = max(dot(N,  L), 0.0);
        float NdotL      = min(NdotL_geom, NdotL_nm);

        if (NdotL > 0.0)
        {
            shadow = shadow_factor(v_world_pos, Ng);
            vec3 radiance = light_ubo.dir_light.color.rgb * shadow;

#if BLEND_MODE_TRANSLUCENT
            Lo += albedo / PI * radiance * NdotL;
#else
            vec3 H = normalize(V + L);
            vec3 F0 = mix(vec3(0.04), albedo, metallic);

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3 spec =
                (NDF * G * F) /
                max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);

            vec3 kS = F;
            vec3 kD = (1.0 - kS) * (1.0 - metallic);

            Lo += (kD * albedo / PI + spec) * radiance * NdotL;
#endif
        }
    }

    // ================== AMBIENT ==================
    vec3 ambient = vec3(0.01) * albedo * ao;

#if BLEND_MODE_TRANSLUCENT
    ambient *= shadow;
#endif

    vec3 color = ambient + Lo;

#if BLEND_MODE_TRANSLUCENT
    out_color = vec4(color, alpha);
#else
    out_color = vec4(color, 1.0);
#endif
}