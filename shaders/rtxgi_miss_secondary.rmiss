#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main()
{
    vec3 sky = vec3(0.03, 0.035, 0.04);
    payload.radiance += payload.throughput * sky;
}