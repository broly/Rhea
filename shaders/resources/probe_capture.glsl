#ifndef RESOURCES_PROBE_CAPTURE
#define RESOURCES_PROBE_CAPTURE

// Runtime reflection probe baking (ReflectionProbeSystem, src/game/reflection_probes.cpp):
// scene -> capture cube (ProbeCapture / ProbeSky), mip chain, then ProbePrefilter / ProbeIrradiance
// read the capture cube through u_probe_source and write the probe's slot.

#ifndef SET_PROBE_CAPTURE
    #define SET_PROBE_CAPTURE 0
    #error "SET_PROBE_CAPTURE definition is missing. Provide this resource: probe_capture"
#endif
#ifndef BINDING_UBO_PROBE_CAPTURE
    #define BINDING_UBO_PROBE_CAPTURE 0
    #error "BINDING_UBO_PROBE_CAPTURE definition is missing. Provide this resource: probe_capture"
#endif
#ifndef BINDING_SAMPLER_PROBE_SOURCE
    #define BINDING_SAMPLER_PROBE_SOURCE 0
    #error "BINDING_SAMPLER_PROBE_SOURCE definition is missing. Provide this resource: probe_capture"
#endif
#ifndef BINDING_SAMPLER_PROBE_SKY_NOISE
    #define BINDING_SAMPLER_PROBE_SKY_NOISE 0
    #error "BINDING_SAMPLER_PROBE_SKY_NOISE definition is missing. Provide this resource: probe_capture"
#endif
#ifndef BINDING_SAMPLER_PROBE_SCRATCH_SPECULAR
    #define BINDING_SAMPLER_PROBE_SCRATCH_SPECULAR 0
    #error "BINDING_SAMPLER_PROBE_SCRATCH_SPECULAR definition is missing. Provide this resource: probe_capture"
#endif
#ifndef BINDING_SAMPLER_PROBE_SCRATCH_IRRADIANCE
    #define BINDING_SAMPLER_PROBE_SCRATCH_IRRADIANCE 0
    #error "BINDING_SAMPLER_PROBE_SCRATCH_IRRADIANCE definition is missing. Provide this resource: probe_capture"
#endif

layout(set = SET_PROBE_CAPTURE, binding = BINDING_UBO_PROBE_CAPTURE)
uniform ProbeCaptureUBO
{
    mat4 face_view_proj[6];  // +X -X +Y -Y +Z -Z
    vec4 capture_position;   // xyz
    vec4 sky_color;          // rgb: sky radiance behind the clouds, a: cloud density scale
    vec4 sky_ambient;        // rgb: ambient of surfaces no probe covers yet
    vec4 filter_params;      // x: source face size, y: source mip count
    // lights baked into the probe (Light::visible_in_reflection_probes)
    vec4 light_position[8];  // xyz
    vec4 light_color[8];     // rgb
    uvec4 light_info;        // x: point light count, y: sun visible (light_ubo.dir_light)
} probe_capture_ubo;

// capture cube (all mips): only the filter passes read it, the capture pass renders into it
layout(set = SET_PROBE_CAPTURE, binding = BINDING_SAMPLER_PROBE_SOURCE)
uniform samplerCube u_probe_source;

layout(set = SET_PROBE_CAPTURE, binding = BINDING_SAMPLER_PROBE_SKY_NOISE)
uniform sampler2D u_probe_sky_noise;

// a filtered capture waiting to be crossfaded into its slot (probe_blend.frag)
layout(set = SET_PROBE_CAPTURE, binding = BINDING_SAMPLER_PROBE_SCRATCH_SPECULAR)
uniform samplerCube u_probe_scratch_specular;

layout(set = SET_PROBE_CAPTURE, binding = BINDING_SAMPLER_PROBE_SCRATCH_IRRADIANCE)
uniform samplerCube u_probe_scratch_irradiance;

// Direction through texel uv ([0,1], v down) of a cube face, Vulkan cubemap layer order.
// Must match ReflectionProbeSystem::face_view_proj.
vec3 probe_face_uv_to_dir(uint face, vec2 uv)
{
    uv = uv * 2.0 - 1.0;
    switch (face)
    {
        case 0u: return normalize(vec3( 1.0, -uv.y, -uv.x));
        case 1u: return normalize(vec3(-1.0, -uv.y,  uv.x));
        case 2u: return normalize(vec3( uv.x,  1.0,  uv.y));
        case 3u: return normalize(vec3( uv.x, -1.0, -uv.y));
        case 4u: return normalize(vec3( uv.x, -uv.y,  1.0));
        default: return normalize(vec3(-uv.x, -uv.y, -1.0));
    }
}

// Sky as the main view shows it (clouds.frag: flat sky color, clouds marched through the noise texture),
// the sun disc added on top so glossy reflections get a highlight.
vec3 probe_sky_radiance(vec3 ray_dir, vec3 sun_dir, vec3 sun_color)
{
    const float cloud_bottom = 100.0;
    const float cloud_top = 300.0;
    const int steps = 32;
    const float step_size = (cloud_top - cloud_bottom) / float(steps);

    vec3 color = probe_capture_ubo.sky_color.rgb;
    float alpha = 0.0;
    vec3 origin = probe_capture_ubo.capture_position.xyz;

    for (int i = 0; i < steps; i++)
    {
        vec3 p = origin + ray_dir * (cloud_bottom + float(i) * step_size);
        float n = textureLod(u_probe_sky_noise, p.xz * 0.001, 0.0).r;
        float density = clamp(n - 0.4, 0.0, 1.0) * 0.5 * probe_capture_ubo.sky_color.a;
        if (density > 0.001)
        {
            // twice the step of clouds.frag: half as many samples
            float a = density * 0.16;
            color = mix(color, vec3(1.0), a);
            alpha += a;
            if (alpha > 0.95)
                break;
        }
    }

    // below the horizon: darker ground bounce instead of mirrored clouds
    float below = clamp(-ray_dir.y * 4.0, 0.0, 1.0);
    color = mix(color, probe_capture_ubo.sky_ambient.rgb, below);

    float sun = pow(max(dot(ray_dir, sun_dir), 0.0), 1500.0);
    color += sun_color * sun * (1.0 - alpha) * (1.0 - below);
    return color;
}

#endif // RESOURCES_PROBE_CAPTURE
