#version 450

#include "definitions.glsl"
#include "pbr_helpers.glsl"
#include "math.glsl"
#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/shadow.glsl"
#include "resources/reflection.glsl"
#include "resources/pbr_material.glsl"

// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;

// ================== OUTPUT ==================
layout(location = 0) out vec4 out_color;
#if !BLEND_MODE_TRANSLUCENT
layout(location = 1) out vec4 out_normal;
#endif 


void main()
{
    vec4 base_tx = texture(u_base_color, v_uv);


#if BLEND_MODE_TRANSLUCENT
    float alpha = base_tx.a;
    
    if (alpha <= 0.001)
        discard;
#endif
    

    // ----- Albedo (linear) -----
    vec3 albedo = pow(base_tx.rgb, vec3(2.2)) * material_ubo.base_color_factor;

    // ----- ORM -----
    vec3 orm = texture(u_orm, v_uv).rgb;
    float ao        = orm.r * material_ubo.occlusion_factor;
    float roughness = orm.g * material_ubo.roughness_factor;
    float metallic  = orm.b * material_ubo.metallic_factor;

#if BLEND_MODE_TRANSLUCENT
    metallic  = 0.0;
    roughness = 1.0;
#endif

    // ----- Normals -----
    vec3 Ng = normalize(v_world_normal);
    vec3 N  = Ng;

    float variance = max(
        dot(dFdx(N), dFdx(N)),
        dot(dFdy(N), dFdy(N))
    );

    roughness = sqrt(roughness * roughness + variance);
    roughness = clamp(roughness, 0.05, 1.0);

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

    const float MAX_REFLECTION_LOD = 6.0;


    float dist = length(camera_ubo.camera_pos.xyz - v_world_pos);

    float lod = roughness * MAX_REFLECTION_LOD;
    lod += 1.0;
    lod += clamp(dist * 0.01, 0.0, 2.0);  // distance bias  

    vec3 prefilteredColor =
    textureLod(u_prefilter_map, R, lod).rgb;

    vec2 brdf = texture(
        u_brdf_lut,
        vec2(max(dot(N, V), 0.0), roughness)
    ).rg;

    vec3 specularIBL = prefilteredColor * (kS * brdf.x + brdf.y);

    vec3 ibl = (kD * diffuseIBL + specularIBL) * (1);
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


    vec3 N_view = normalize((camera_ubo.view * vec4(N, 0.0)).xyz);
    out_normal = vec4(N_view * 0.5 + 0.5, roughness);
    
#endif
    
    
}