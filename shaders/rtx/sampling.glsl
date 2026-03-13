#ifndef RTX_SAMPLING
#define RTX_SAMPLING

#define PI 3.1415926535

vec3 cosine_sample_hemisphere(vec3 N, inout uint seed)
{
    float r1 = rand(seed);
    float r2 = rand(seed);

    float phi = 2.0 * PI * r1;

    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    vec3 T = normalize(
        abs(N.y) < 0.999
        ? cross(N, vec3(0,1,0))
        : cross(N, vec3(1,0,0))
    );

    vec3 B = cross(N, T);

    return normalize(
        T * cos(phi) * sinTheta +
        B * sin(phi) * sinTheta +
        N * cosTheta
    );
}

#endif  // RTX_SAMPLING