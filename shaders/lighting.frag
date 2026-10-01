#version 450

#include "resources/gbuffer.glsl"
#include "resources/dbuffer.glsl"
#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/shadow.glsl"
#include "resources/reflection.glsl"
#include "resources/gtao.glsl"
#include "pbr_helpers.glsl"
#include "character/character_lighting.glsl"
#include "utils/gbuffer_position.glsl"

layout(location = 0) out vec4 out_color;
// hdr_color_present[SPECULAR_WEIGHT]: rgb = what the reflected radiance is multiplied by (env BRDF x
// specular occlusion). The specular itself is added by SSRComposite: SSR where the ray hit, the reflection
// probes elsewhere (ssr_composite.frag)
layout(location = 1) out vec4 out_specular_weight;
layout(location = 0) in vec2 v_uv;

layout(push_constant)
uniform ColorOutputConstants
{
    uint buffer_index;
    uint debug_flags;  // LightingDebug bits (generic_render_graph.ixx)
    uint gtao_downscale;    // screen pixels per texel of u_gtao, 0: no GTAO
    float gtao_foliage;     // strength of GTAO on leaves
} pc;

const uint LIGHTING_DEBUG_NO_DIFFUSE_GI = 1u;
const uint LIGHTING_DEBUG_NO_SPECULAR_IBL = 2u;
const uint LIGHTING_DEBUG_NO_EMISSIVE = 4u;
const uint LIGHTING_DEBUG_NO_SHADOWS = 8u;
const uint LIGHTING_DEBUG_NO_DECALS = 16u;
const uint LIGHTING_DEBUG_EMISSIVE_WATCH = 32u;
const uint LIGHTING_DEBUG_PROBE_MIRROR = 64u;

bool lighting_debug(uint flag)
{
    return (pc.debug_flags & flag) != 0u;
}

// GTAO visibility at a screen pixel (GtaoRenderer). Downscaled: the 4 texels around the pixel, bilinear
// weights times how well the depth each texel was computed for matches the pixel's (no halos across edges);
// none matching: the closest in depth.
float gtao_visibility(ivec2 pixel, float depth)
{
    if (pc.gtao_downscale == 0u || depth <= 0.0)
        return 1.0;
    if (pc.gtao_downscale == 1u)
        return texelFetch(u_gtao, pixel, 0).r;

    const ivec2 size = textureSize(u_gtao, 0);
    const vec2 position = (vec2(pixel) + 0.5) / float(pc.gtao_downscale) - 0.5;
    const ivec2 base = ivec2(floor(position));
    const vec2 f = position - vec2(base);

    float sum = 0.0;
    float weight_sum = 0.0;
    float closest = 1.0;
    float closest_difference = 1.0e30;
    for (int i = 0; i < 4; ++i)
    {
        const ivec2 offset = ivec2(i & 1, i >> 1);
        const vec4 texel = texelFetch(u_gtao, clamp(base + offset, ivec2(0), size - 1), 0);
        const vec2 bilinear = mix(1.0 - f, f, vec2(offset));
        const float difference = abs(texel.g - depth);
        const float weight = bilinear.x * bilinear.y * exp(-difference / (0.02 * depth));
        sum += texel.r * weight;
        weight_sum += weight;
        if (difference < closest_difference)
        {
            closest_difference = difference;
            closest = texel.r;
        }
    }
    return weight_sum > 1e-4 ? sum / weight_sum : closest;
}

// Light bouncing between the occluders brightens the occlusion of bright surfaces (Jimenez et al. 2016,
// fitted to ray traced multi bounce AO)
vec3 ao_multi_bounce(float visibility, vec3 albedo)
{
    const vec3 a = 2.0404 * albedo - 0.3324;
    const vec3 b = -4.7951 * albedo + 0.6417;
    const vec3 c = 2.7552 * albedo + 0.6903;
    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

void main()
{
    vec2 uv = v_uv;
    out_specular_weight = vec4(0.0);
    
    vec4 albedo_roughness = get_gbuffer_ALBEDO_ROUGHNESS(uv);
    
    vec4 decal = texture(u_decal_albedo, uv);
    if (lighting_debug(LIGHTING_DEBUG_NO_DECALS))
        decal = vec4(0.0);

    vec3 albedo = mix(albedo_roughness.rgb, decal.rgb, decal.a);
    vec3 N  = normalize(get_gbuffer_WORLD_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 Ng = normalize(get_gbuffer_GEOMETRY_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 pos = gbuffer_world_position(ivec2(gl_FragCoord.xy));
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
    
    // ---- character shading models (see character/shading_models.glsl) ----
    uint shading_model = decode_shading_model(get_gbuffer_GEOMETRY_NORMAL(uv).a);
    // foliage: the legacy model plus transmission, g_emissive holds its color instead of emissive
    const bool foliage_model = shading_model == SHADING_MODEL_ID_FOLIAGE;
    const bool legacy_model = shading_model == SHADING_MODEL_ID_CLEAR || shading_model == SHADING_MODEL_ID_LEGACY
        || foliage_model;
    const vec3 transmission = foliage_model ? emissive : vec3(0.0);
    if (foliage_model)
        emissive = vec3(0.0);
    
    if (lighting_debug(LIGHTING_DEBUG_PROBE_MIRROR))
    {
        out_color = vec4(sample_reflection_probes_specular(pos, reflect(-V, N), 0.0), 1.0);
        return;
    }
    
    // Same roughness clamp as character_decode_gbuffer
    const float roughness = clamp(albedo_roughness.a, 0.04, 1.0);
    const float NdotV = max(dot(N, V), 1e-4);
    
    // buffer_index == LIGHTING_GI_FROM_IBL (ray tracing disabled): ambient from the reflection probes
    // (blended by their boxes, resources/reflection.glsl)
    const bool gi_from_ibl = pc.buffer_index == 0xFFFFFFFFu;
    vec3 gi = gi_from_ibl
        ? sample_reflection_probes_irradiance(pos, N)
        : texture(u_hdr_color_present[pc.buffer_index], uv).rgb;
    if (lighting_debug(LIGHTING_DEBUG_NO_DIFFUSE_GI))
        gi = vec3(0.0);

    // ambient occlusion: the material's (texture) and the screen space one, whichever occludes more
    const float linear_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], ivec2(gl_FragCoord.xy), 0).r;
    float screen_ao = gtao_visibility(ivec2(gl_FragCoord.xy), linear_depth);
    if (foliage_model)
        screen_ao = mix(1.0, screen_ao, pc.gtao_foliage);
    const float ao = min(get_gbuffer_WORLD_NORMAL(uv).a, screen_ao);
    
    if (!legacy_model)
    {
        CharacterGBuffer g = character_decode_gbuffer(
            shading_model,
            vec4(albedo, albedo_roughness.a),
            N,
            ao,
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
        
        vec3 character_specular_weight;
        radiance += character_eval_indirect(g, V, gi, character_specular_weight);
        if (!lighting_debug(LIGHTING_DEBUG_NO_EMISSIVE))
            radiance += g.emissive;
        
        out_color = vec4(radiance, 1.0);
        if (!lighting_debug(LIGHTING_DEBUG_NO_SPECULAR_IBL))
            out_specular_weight = vec4(character_specular_weight, 1.0);
        return;
    }

    // ---- legacy PBR model ----
    const float metallic = clamp(get_gbuffer_EMISSIVE(uv).a, 0.0, 1.0);
    vec3 diffuse_albedo = albedo * (1.0 - metallic);
    
    vec3 direct = vec3(0.0);

    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - pos);
        float NdotL = dot(N, L);

        float dist = length(Lpos - pos);
        float attenuation = 1.0 / (dist * dist + 1.0);

        // a leaf lit from behind passes a part of the light through
        direct += (diffuse_albedo * max(NdotL, 0.0) + transmission * max(-NdotL, 0.0)) / 3.14159265 * Lcol * attenuation;
    }
    
    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = dot(N, L);

        if (NdotL > 0.0 || foliage_model)
        {
            // the shadow lookup is offset along the normal, away from the surface: for a leaf, on its lit side
            float shadow = lighting_debug(LIGHTING_DEBUG_NO_SHADOWS) ? 1.0 : shadow_factor(pos, foliage_model && dot(Ng, L) < 0.0 ? -Ng : Ng);
            vec3 radiance = light_ubo.dir_light.color.rgb * shadow;

            direct += (diffuse_albedo * max(NdotL, 0.0) + transmission * max(-NdotL, 0.0)) / 3.14159265 * radiance;
        }
    }

    vec3 indirect = gi * diffuse_albedo * ao_multi_bounce(ao, diffuse_albedo);

    vec3 color = direct + indirect + emissive;

    out_color = vec4(color, 1.0);
    
    // image based specular (split sum with the analytic env BRDF, occluded by the material AO), applied in
    // SSRComposite to SSR / reflection probe radiance
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 specular_weight = env_brdf_approx(F0, roughness, NdotV) * specular_occlusion(NdotV, ao, roughness);
    // leaves are seen edge on all over a crown: the Fresnel peak there would mirror the sky in single pixels
    // (the crown sparkles), and most of what a leaf reflects is other leaves, not the sky
    if (foliage_model)
        specular_weight = env_brdf_approx(F0, max(roughness, 0.6), max(NdotV, 0.5)) * 0.4 * ao;
    if (!lighting_debug(LIGHTING_DEBUG_NO_SPECULAR_IBL))
        out_specular_weight = vec4(specular_weight, 1.0);
}