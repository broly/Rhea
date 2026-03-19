#ifndef RTX_RAY_PAYLOAD
#define RTX_RAY_PAYLOAD


struct RayPayload
{
    vec3 radiance;
    vec3 throughput;
    uint depth;
};

struct ShadowPayload
{
    bool hit;
};

#endif // RTX_RAY_PAYLOAD
