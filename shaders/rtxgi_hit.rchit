#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"


#if 0
    // stub
    #define rayPayloadInEXT
#endif

layout(location = RTXGI_RAY_PAYLOAD_SHADOW) 
rayPayloadInEXT ShadowPayload payload;

void main()
{
    payload.hit = true;
}