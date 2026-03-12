#ifndef RTX_RAY_PAYLOAD
#define RTX_RAY_PAYLOAD


struct RayPayload
{
    vec3 radiance;
    vec3 throughput;

    uint seed;
    int depth;
};


#endif // RTX_RAY_PAYLOAD
