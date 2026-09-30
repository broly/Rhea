#version 450

// Background of a reflection probe capture face (drawn first, the scene goes on top): the sky of the
// main view seen from the capture point, with coarser clouds.

#include "resources/probe_capture.glsl"
#include "resources/sky.glsl"
#include "sky/atmosphere.glsl"
#include "sky/clouds.glsl"

layout(push_constant) uniform ProbeFacePushConstants
{
    uint face;
    uint mip;
    float roughness;
    uint sample_count;
    float blend;        // output alpha: crossfade weight of the new capture (blend pipelines)
} probe_face;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

const int PROBE_CLOUD_STEPS = 24;
const int PROBE_CLOUD_LIGHT_STEPS = 3;

void main()
{
    vec3 dir = probe_face_uv_to_dir(probe_face.face, v_uv);
    vec3 origin = probe_capture_ubo.capture_position.xyz;

    // no discs: a texel of the capture is wider than the sun
    vec3 color = sky_radiance(origin.y, dir, false);

    // the light which lights the scene of the capture, as a lobe the filtering of the probe can take
    // (glossy reflections get their highlight)
    if (probe_capture_ubo.light_info.y != 0u)
    {
        float lobe = pow(max(dot(dir, sky_ubo.light_direction.xyz), 0.0), 1500.0);
        color += sky_ubo.light_color.rgb * lobe * step(0.0, dir.y);
    }

    float cloud_distance;
    vec4 clouds = cloud_march(origin, dir, 0.5, PROBE_CLOUD_STEPS, PROBE_CLOUD_LIGHT_STEPS, cloud_distance);
    color = color * clouds.a + clouds.rgb;

    out_color = vec4(color, 1.0);
}
