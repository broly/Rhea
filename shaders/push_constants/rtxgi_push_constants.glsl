#ifndef RTXGI_PUSH_CONSTANTS
#define RTXGI_PUSH_CONSTANTS

layout(push_constant)
uniform RTXGIPushConstants
{
    uint frame;
    float intensity;
    uint spp;
} pc;

#endif // RTXGI_PUSH_CONSTANTS