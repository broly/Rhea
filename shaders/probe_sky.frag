#version 450

// Background of a reflection probe capture face (drawn first, the scene goes on top).

#include "resources/probe_capture.glsl"
#include "resources/light.glsl"

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

void main()
{
    vec3 dir = probe_face_uv_to_dir(probe_face.face, v_uv);

    vec3 sun_dir = vec3(0.0, 1.0, 0.0);
    vec3 sun_color = vec3(0.0);
    if (light_ubo.has_dir_light == 1 && probe_capture_ubo.light_info.y != 0u)
    {
        sun_dir = normalize(-light_ubo.dir_light.direction.xyz);
        sun_color = light_ubo.dir_light.color.rgb;
    }

    out_color = vec4(probe_sky_radiance(dir, sun_dir, sun_color), 1.0);
}
