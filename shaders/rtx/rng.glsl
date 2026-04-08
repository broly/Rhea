#ifndef RTX_RNG
#define RTX_RNG

#include "utils/math.glsl"



uint wang_hash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float rand(inout uint seed)
{
    seed = wang_hash(seed);
    return float(seed) / float(0xffffffffu);
}

vec3 sample_cosine_hemisphere(vec3 N, float u1, float u2)
{
    float r = sqrt(u1);
    float theta = 2.0 * 3.14159265 * u2;

    vec3 T = normalize(abs(N.y) < 0.999 ? cross(N, vec3(0,1,0)) : cross(N, vec3(1,0,0)));
    vec3 B = cross(N, T);

    vec3 smpl = vec3(
        r * cos(theta),
        r * sin(theta),
        sqrt(max(0.0, 1.0 - u1))
    );

    return T * smpl.x + B * smpl.y + N * smpl.z;
}

// Cosine hemisphere sampling
vec3 cosine_sample(vec3 N, in uint seed)
{
    float r1 = rand(seed);
    float r2 = rand(seed);

    float phi = 2.0 * 3.14159265 * r1;
    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    vec3 T = normalize(abs(N.x) > 0.1 ? cross(vec3(0,1,0), N) : cross(vec3(1,0,0), N));
    vec3 B = cross(N, T);

    return normalize(
        T * cos(phi) * sinTheta +
        B * sin(phi) * sinTheta +
        N * cosTheta
    );
}


#endif // RTX_RNG