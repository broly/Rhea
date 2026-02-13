#version 450

#include "definitions.glsl"
#include "pbr_helpers.glsl"
#include "math.glsl"

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
    mat4 proj;
    mat4 view;
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



// ================== SHADOW ==================

layout(set = SET_SHADOW_RESOURCE, binding = BINDING_UBO_SHADOW) uniform sampler2D u_shadow_depth;


#define WITH_POISSON 1
const int POISSON_STEPS = 8;

const vec2 POISSON[8] = vec2[](
    vec2(-0.326, -0.406),
    vec2(-0.840, -0.074),
    vec2(-0.696,  0.457),
    vec2(-0.203,  0.621),
    vec2( 0.962, -0.195),
    vec2( 0.473, -0.480),
    vec2( 0.519,  0.767),
    vec2( 0.185, -0.893)
);

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

mat2 rot2(float a)
{
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

float shadow_factor(vec3 world_pos, vec3 Ng)
{
    vec3 shadow_pos = world_pos + Ng * 0.003;

    vec4 light_clip = light_ubo.dir_light.light_vp * vec4(shadow_pos, 1.0);
    vec3 proj = light_clip.xyz / light_clip.w;

    if (proj.x < -1.0 || proj.x > 1.0 ||
    proj.y < -1.0 || proj.y > 1.0 ||
    proj.z <  0.0 || proj.z > 1.0)
    return 1.0;

    vec2 uv = proj.xy * 0.5 + 0.5;

    float ndotl = max(dot(Ng, -light_ubo.dir_light.direction.xyz), 0.0);
    float bias = mix(0.0005, 0.0025, 1.0 - ndotl);

    vec2 texelSize = 1.0 / vec2(textureSize(u_shadow_depth, 0));

    
#if WITH_POISSON
    float radius = mix(1.25, 2.5, 1.0 - ndotl);
    
    // --- rotate kernel per-pixel
    float angle = hash12(gl_FragCoord.xy) * 6.2831853;
    mat2 R = rot2(angle);
    float shadow = 0.0;

    for (int i = 0; i < POISSON_STEPS; ++i)
    {
        vec2 offset = R * POISSON[i] * texelSize * radius;
        float depth = texture(u_shadow_depth, uv + offset).r;
        shadow += (proj.z - bias <= depth) ? 1.0 : 0.0;
    }

    return shadow / POISSON_STEPS;
#else
    float depth = texture(u_shadow_depth, uv).r;
    return (proj.z - bias > depth) ? 0.0 : 1.0;
#endif    
}

// ============= reflection =================

layout(set = SET_IBL, binding = BINDING_SAMPLER_IRRADIANCE) uniform samplerCube u_irradiance;
layout(set = SET_IBL, binding = BINDING_SAMPLER_PREFILTERED_ENV) uniform samplerCube u_prefilter_map;
layout(set = SET_IBL, binding = BINDING_SAMPLER_BRDF_LUT) uniform sampler2D u_brdf_lut;


vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
    pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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


#if !BLEND_MODE_TRANSLUCENT
    // ================== IBL ==================
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ---- Diffuse IBL ----
    vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(u_irradiance, N).rgb;
    
    vec3 diffuseIBL = irradiance * albedo;

    // ---- Specular IBL ----
    vec3 R = reflect(-V, N);

    const float MAX_REFLECTION_LOD = 5.0;
    vec3 prefilteredColor =
    textureLod(u_prefilter_map, R, roughness * MAX_REFLECTION_LOD).rgb;

    vec2 brdf = texture(
        u_brdf_lut,
        vec2(max(dot(N, V), 0.0), roughness)
    ).rg;

    vec3 specularIBL =
    prefilteredColor * (kS * brdf.x + brdf.y);

    vec3 ibl = (kD * diffuseIBL + specularIBL) * ao;
#endif
    
#if BLEND_MODE_TRANSLUCENT
    vec3 ambient = vec3(0.01) * albedo * ao;
#else
    vec3 ambient = ibl;
#endif


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