#version 450

#include "resources/gbuffer.glsl"
#include "resources/dbuffer.glsl"
#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/shadow.glsl"
#include "resources/reflection.glsl"
#include "pbr_helpers.glsl"
#include "character/character_lighting.glsl"

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec2 v_uv;

layout(push_constant)
uniform ColorOutputConstants
{
    uint buffer_index;
    uint debug_flags;  // LightingDebug bits (generic_render_graph.ixx)
} pc;

const uint LIGHTING_DEBUG_NO_DIFFUSE_GI = 1u;
const uint LIGHTING_DEBUG_NO_SPECULAR_IBL = 2u;
const uint LIGHTING_DEBUG_NO_EMISSIVE = 4u;
const uint LIGHTING_DEBUG_NO_SHADOWS = 8u;
const uint LIGHTING_DEBUG_NO_DECALS = 16u;
const uint LIGHTING_DEBUG_EMISSIVE_WATCH = 32u;

bool lighting_debug(uint flag)
{
    return (pc.debug_flags & flag) != 0u;
}

void main()
{
    vec2 uv = v_uv;
    
    vec4 albedo_roughness = get_gbuffer_ALBEDO_ROUGHNESS(uv);
    
    vec4 decal = texture(u_decal_albedo, uv);
    if (lighting_debug(LIGHTING_DEBUG_NO_DECALS))
        decal = vec4(0.0);

    vec3 albedo = mix(albedo_roughness.rgb, decal.rgb, decal.a);
    vec3 N  = normalize(get_gbuffer_WORLD_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 Ng = normalize(get_gbuffer_GEOMETRY_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 pos = get_gbuffer_POSITION(uv).rgb;
    vec3 emissive = get_gbuffer_EMISSIVE(uv).rgb;
    
    // emissive watch: only the screen periphery is checked (the level, which has no emissive textures;
    // the character in the middle has legit emissive parts). Non-zero emissive there is corruption.
    // Discarded pixels are counted by an occlusion query (GenericRenderGraph::read_diag_queries)
    if (lighting_debug(LIGHTING_DEBUG_EMISSIVE_WATCH))
    {
        const bool periphery = uv.x < 0.2 || uv.x > 0.8 || uv.y < 0.15 || uv.y > 0.85;
        const uint watch_model = decode_shading_model(get_gbuffer_GEOMETRY_NORMAL(uv).a);
        const bool legacy = watch_model == SHADING_MODEL_ID_CLEAR || watch_model == SHADING_MODEL_ID_LEGACY;
        if (periphery && legacy && max(emissive.r, max(emissive.g, emissive.b)) > 0.02)
            discard;
    }
    if (lighting_debug(LIGHTING_DEBUG_NO_EMISSIVE))
        emissive = vec3(0.0);

    vec3 V = normalize(camera_ubo.camera_pos.xyz - pos);
    
    // buffer_index == LIGHTING_GI_FROM_IBL (ray tracing disabled): ambient from the reflection capture
    const bool gi_from_ibl = pc.buffer_index == 0xFFFFFFFFu;
    vec3 gi = gi_from_ibl
        ? texture(u_irradiance, N).rgb
        : texture(u_hdr_color_present[pc.buffer_index], uv).rgb;
    if (lighting_debug(LIGHTING_DEBUG_NO_DIFFUSE_GI))
        gi = vec3(0.0);
    
    // ---- character shading models (see character/shading_models.glsl) ----
    uint shading_model = decode_shading_model(get_gbuffer_GEOMETRY_NORMAL(uv).a);
    
    if (shading_model != SHADING_MODEL_ID_CLEAR && shading_model != SHADING_MODEL_ID_LEGACY)
    {
        CharacterGBuffer g = character_decode_gbuffer(
            shading_model,
            vec4(albedo, albedo_roughness.a),
            N,
            get_gbuffer_WORLD_NORMAL(uv).a,
            get_gbuffer_EMISSIVE(uv));
        
        vec3 radiance = vec3(0.0);
        
        for (int i = 0; i < light_ubo.light_count; ++i)
        {
            vec3 Lpos = light_ubo.lights[i].position.xyz;
            vec3 to_light = Lpos - pos;
            float dist = length(to_light);
            float attenuation = 1.0 / (dist * dist + 1.0);
            radiance += character_eval_light(g, V, to_light / dist, light_ubo.lights[i].color.rgb * attenuation);
        }
        
        if (light_ubo.has_dir_light == 1)
        {
            vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
            float shadow = lighting_debug(LIGHTING_DEBUG_NO_SHADOWS) ? 1.0 : shadow_factor(pos, Ng);
            radiance += character_eval_light(g, V, L, light_ubo.dir_light.color.rgb * shadow);
        }
        
        radiance += character_eval_indirect(g, V, gi, pos, !lighting_debug(LIGHTING_DEBUG_NO_SPECULAR_IBL));
        if (!lighting_debug(LIGHTING_DEBUG_NO_EMISSIVE))
            radiance += g.emissive;
        
        out_color = vec4(radiance, 1.0);
        return;
    }

    // ---- legacy PBR model ----
    vec3 direct = vec3(0.0);

    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - pos);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL <= 0.0) 
            continue;

        float dist = length(Lpos - pos);
        float attenuation = 1.0 / (dist * dist + 1.0);

        direct += albedo / 3.14159265 * Lcol * attenuation * NdotL;
    }
    
    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            float shadow = lighting_debug(LIGHTING_DEBUG_NO_SHADOWS) ? 1.0 : shadow_factor(pos, Ng);
            vec3 radiance = light_ubo.dir_light.color.rgb * shadow;

            direct += albedo / 3.14159265 * radiance * NdotL;
        }
    }

    vec3 indirect = gi * albedo;

    vec3 color = direct + indirect + emissive;

    out_color = vec4(color, 1.0);
}