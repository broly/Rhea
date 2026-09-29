#version 450

// Reflection probe crossfade update (ReflectionProbeSystem::blend_into_slot): the new capture, already
// filtered into the scratch cubes, alpha blended over the probe's slot (fixed function blending with
// src alpha = probe_face.blend). One texel read per texel: texel centers of the same mip size.

#include "resources/probe_capture.glsl"

layout(push_constant) uniform ProbeFacePushConstants
{
    uint face;
    uint mip;
    float roughness;    // unused
    uint sample_count;  // source: 0 = specular (at mip), 1 = irradiance
    float blend;        // output alpha: weight of the new capture
} probe_face;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    vec3 dir = probe_face_uv_to_dir(probe_face.face, v_uv);
    vec3 color = probe_face.sample_count == 0u
        ? textureLod(u_probe_scratch_specular, dir, float(probe_face.mip)).rgb
        : textureLod(u_probe_scratch_irradiance, dir, 0.0).rgb;
    out_color = vec4(color, probe_face.blend);
}
