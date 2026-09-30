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
    vec4 filter_params;      // x: source face size, y: source mip count
    // lights baked into the probe (Light::visible_in_reflection_probes)
    vec4 light_position[8];  // xyz
    vec4 light_color[8];     // rgb
    uvec4 light_info;        // x: point light count, y: sun visible (light_ubo.dir_light)
} probe_capture_ubo;

// capture cube (all mips): only the filter passes read it, the capture pass renders into it
layout(set = SET_PROBE_CAPTURE, binding = BINDING_SAMPLER_PROBE_SOURCE)
uniform samplerCube u_probe_source;

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

#endif // RESOURCES_PROBE_CAPTURE
