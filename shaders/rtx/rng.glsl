#ifndef RTX_RNG
#define RTX_RNG

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
    return float(seed) / 4294967296.0;
}

uint tea(uint v0, uint v1)
{
    uint s0 = 0;

    for (uint n = 0; n < 4; n++)
    {
        s0 += 0x9e3779b9;

        v0 += ((v1 << 4) + 0xa341316c) ^
        (v1 + s0) ^
        ((v1 >> 5) + 0xc8013ea4);

        v1 += ((v0 << 4) + 0xad90777d) ^
        (v0 + s0) ^
        ((v0 >> 5) + 0x7e95761e);
    }

    return v0;
}

#endif // RTX_RNG