#ifndef RTX_RAY_PAYLOAD
#define RTX_RAY_PAYLOAD


struct RayPayload
{
    vec3 radiance;
    vec3 throughput;

    uint seed;
    int depth;

    int ray_type;   // 0 = path, 1 = shadow
    bool shadow_hit;
};

struct ShadowPayload
{
    bool hit;
};

#endif // RTX_RAY_PAYLOAD
