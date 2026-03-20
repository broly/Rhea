#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;

layout(push_constant) uniform RTXGIPushConstants
{
    uint frame;
    float intensity;
} pc;

void main()
{
    vec3 dir = normalize(gl_WorldRayDirectionEXT);

    float t = 0.5 * (dir.y + 1.0);

    vec3 sky = mix(
        vec3(0.1, 0.15, 0.3),
        vec3(0.7, 0.8, 1.0),
        t
    );

    payload.radiance += payload.throughput * sky;
}