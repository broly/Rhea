#ifndef RTX_RAY_PAYLOAD
#define RTX_RAY_PAYLOAD

#define RTXGI_RAY_PAYLOAD_RAY 0
struct RayPayload
{
    vec3 radiance;
    vec3 throughput;
    uint depth;
};

#define RTXGI_RAY_PAYLOAD_SHADOW 1
struct ShadowPayload
{
    bool hit;
};

#endif // RTX_RAY_PAYLOAD
