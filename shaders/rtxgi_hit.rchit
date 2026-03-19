#version 460
#extension GL_EXT_ray_tracing : require

#include "rtx/ray_payload.glsl"

layout(location = 1) rayPayloadInEXT ShadowPayload payload;

void main()
{
    payload.hit = true;
}