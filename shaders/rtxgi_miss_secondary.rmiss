#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"

#if 0
    // stub
    #define rayPayloadInEXT
#endif

layout(location = RTXGI_RAY_PAYLOAD_RAY) 
rayPayloadInEXT RayPayload payload;


void main()
{
    vec3 dir = normalize(gl_WorldRayDirectionEXT);

    float t = 0.5 * (dir.y + 1.0);
    
    vec3 sky = vec3(0.0, 0.0, 0.0);

    payload.radiance += payload.throughput * sky;
}