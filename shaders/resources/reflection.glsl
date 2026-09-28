#ifndef RESOURCES_REFLECTION
#define RESOURCES_REFLECTION

// Runtime baked reflection probes (ReflectionProbeSystem, src/game/reflection_probes.cpp).
// Every probe owns a slot: a prefiltered specular cube (roughness in the mips) and a small irradiance cube,
// plus an axis aligned box used both as the influence volume (weight fades in over blend distance) and as
// the parallax proxy (reflection rays are intersected with the box before the cube lookup).
// Probes are blended by weight (reflection_probe_weights): full inside a box, fading out around it, the
// space no box covers interpolated between all probes. Sky ambient only while no probe is baked.

#ifndef SET_IBL
    #define SET_IBL 0
    #error "SET_IBL definition is missing. Provide this resource: reflection"
#endif
#ifndef BINDING_SAMPLER_PROBE_IRRADIANCE
    #define BINDING_SAMPLER_PROBE_IRRADIANCE 0
    #error "BINDING_SAMPLER_PROBE_IRRADIANCE definition is missing. Provide this resource: reflection"
#endif
#ifndef BINDING_SAMPLER_PROBE_SPECULAR
    #define BINDING_SAMPLER_PROBE_SPECULAR 0
    #error "BINDING_SAMPLER_PROBE_SPECULAR definition is missing. Provide this resource: reflection"
#endif
#ifndef BINDING_UBO_REFLECTION_PROBES
    #define BINDING_UBO_REFLECTION_PROBES 0
    #error "BINDING_UBO_REFLECTION_PROBES definition is missing. Provide this resource: reflection"
#endif
#ifndef BINDING_SAMPLER_BRDF_LUT
    #define BINDING_SAMPLER_BRDF_LUT 0
    #error "BINDING_SAMPLER_BRDF_LUT definition is missing. Provide this resource: reflection"
#endif

// kMaxReflectionProbes (system_ubo.ixx), initial_array_size in resources/reflection.json
#define MAX_REFLECTION_PROBES 16

struct GPUReflectionProbe
{
    vec4 capture_position;  // xyz, w: 1 = baked
    vec4 box_min;           // xyz, w: blend distance
    vec4 box_max;           // xyz, w: intensity
};

layout(set = SET_IBL, binding = BINDING_UBO_REFLECTION_PROBES)
uniform ReflectionProbesUBO
{
    GPUReflectionProbe probes[MAX_REFLECTION_PROBES];
    uvec4 info;         // x: slots to scan, y: unused, z: specular mips, w: parallax on
    vec4 sky_ambient;   // rgb
    vec4 intensity;     // x: diffuse, y: specular (0: off)
    vec4 ssr_params;    // x: SSR intensity (0: off), used by ssr_composite.frag
} reflection_probes_ubo;

layout(set = SET_IBL, binding = BINDING_SAMPLER_PROBE_IRRADIANCE)
uniform samplerCube u_probe_irradiance[MAX_REFLECTION_PROBES];

layout(set = SET_IBL, binding = BINDING_SAMPLER_PROBE_SPECULAR)
uniform samplerCube u_probe_specular[MAX_REFLECTION_PROBES];

layout(set = SET_IBL, binding = BINDING_SAMPLER_BRDF_LUT)
uniform sampler2D u_brdf_lut;


vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
    pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Karis, mobile env BRDF approximation (split sum scale / bias applied to F0)
vec3 env_brdf_approx(vec3 F0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// Lagarde: specular occlusion from ambient occlusion
float specular_occlusion(float NdotV, float ao, float roughness)
{
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Distance from pos to the probe box, 0 inside
float reflection_probe_box_distance(in GPUReflectionProbe probe, vec3 pos)
{
    vec3 outside = max(max(probe.box_min.xyz - pos, pos - probe.box_max.xyz), vec3(0.0));
    return length(outside);
}

// Box projected ("parallax corrected") lookup direction: the reflection ray from pos is intersected with the
// probe box and the hit point is looked up from the capture point. Exact for mirrors, rough lobes lean
// back to R (the correction is only right for the lobe center, and a wide lobe hides the error anyway).
// Continuous across the box faces (outside the box t clamps to 0 where the ray leaves it), so the fade
// region around the box shows no seam.
vec3 reflection_probe_parallax_dir(in GPUReflectionProbe probe, vec3 pos, vec3 R, float roughness)
{
    vec3 safe_R = mix(R, vec3(1e-6), lessThan(abs(R), vec3(1e-6)));
    vec3 t_max = (probe.box_max.xyz - pos) / safe_R;
    vec3 t_min = (probe.box_min.xyz - pos) / safe_R;
    vec3 t_far = max(t_max, t_min);
    float t = max(min(t_far.x, min(t_far.y, t_far.z)), 0.0);
    vec3 dir = pos + R * t - probe.capture_position.xyz;
    float len = length(dir);
    if (len < 1e-4)
        return R;
    return normalize(mix(dir / len, R, roughness * roughness));
}

// Blend weights of all probes at pos (sum 1, or 0 when no probe is baked). Continuous in space and
// independent of the camera:
//  - influence: 1 inside a probe box, fading to 0 over blend distance outside of it. Surfaces lying on the
//    box faces (floor, walls the box was fitted to) get the full weight.
//  - overlapping influences are normalized.
//  - the part no influence covers is shared by all probes by inverse square distance to their boxes.
bool reflection_probe_weights(vec3 pos, out float weights[MAX_REFLECTION_PROBES])
{
    const uint count = min(reflection_probes_ubo.info.x, uint(MAX_REFLECTION_PROBES));

    float fallback[MAX_REFLECTION_PROBES];
    float total = 0.0;
    float fallback_total = 0.0;
    for (uint i = 0u; i < uint(MAX_REFLECTION_PROBES); ++i)
    {
        weights[i] = 0.0;
        fallback[i] = 0.0;
        if (i >= count)
            continue;
        GPUReflectionProbe probe = reflection_probes_ubo.probes[i];
        if (probe.capture_position.w == 0.0)
            continue;

        float distance = reflection_probe_box_distance(probe, pos);
        float blend = max(probe.box_min.w, 1e-3);
        weights[i] = 1.0 - smoothstep(0.0, blend, distance);
        fallback[i] = 1.0 / (0.25 + distance * distance);
        total += weights[i];
        fallback_total += fallback[i];
    }

    if (fallback_total <= 0.0)
        return false;

    if (total >= 1.0)
    {
        for (uint i = 0u; i < count; ++i)
            weights[i] /= total;
    }
    else
    {
        float rest = (1.0 - total) / fallback_total;
        for (uint i = 0u; i < count; ++i)
            weights[i] += rest * fallback[i];
    }
    return true;
}

// Diffuse irradiance at pos (already divided by PI: multiply by the albedo)
vec3 sample_reflection_probes_irradiance(vec3 pos, vec3 N)
{
    float weights[MAX_REFLECTION_PROBES];
    if (!reflection_probe_weights(pos, weights))
        return reflection_probes_ubo.sky_ambient.rgb * reflection_probes_ubo.intensity.x;

    const uint count = min(reflection_probes_ubo.info.x, uint(MAX_REFLECTION_PROBES));
    vec3 irradiance = vec3(0.0);
    // explicit LODs everywhere: the loop is divergent
    for (uint i = 0u; i < count; ++i)
    {
        if (weights[i] < 1e-3)
            continue;
        irradiance += weights[i] * reflection_probes_ubo.probes[i].box_max.w * textureLod(u_probe_irradiance[i], N, 0.0).rgb;
    }
    return irradiance * reflection_probes_ubo.intensity.x;
}

// Prefiltered specular radiance along R at pos (multiply by the specular weight: env BRDF, occlusion)
vec3 sample_reflection_probes_specular(vec3 pos, vec3 R, float roughness)
{
    float weights[MAX_REFLECTION_PROBES];
    if (!reflection_probe_weights(pos, weights))
        return reflection_probes_ubo.sky_ambient.rgb * reflection_probes_ubo.intensity.y;

    const uint count = min(reflection_probes_ubo.info.x, uint(MAX_REFLECTION_PROBES));
    const bool parallax = reflection_probes_ubo.info.w != 0u;
    const float lod = roughness * float(max(reflection_probes_ubo.info.z, 1u) - 1u);
    vec3 specular = vec3(0.0);
    for (uint i = 0u; i < count; ++i)
    {
        if (weights[i] < 1e-3)
            continue;
        GPUReflectionProbe probe = reflection_probes_ubo.probes[i];
        vec3 dir = parallax ? reflection_probe_parallax_dir(probe, pos, R, roughness) : R;
        specular += weights[i] * probe.box_max.w * textureLod(u_probe_specular[i], dir, lod).rgb;
    }
    return specular * reflection_probes_ubo.intensity.y;
}


#endif // RESOURCES_REFLECTION
