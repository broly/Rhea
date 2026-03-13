#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main()
{
    if (payload.ray_type == 1)
    {
        payload.shadow_hit = false;
        return;
    }

    // environment
    payload.radiance += payload.throughput * vec3(0.0);
}