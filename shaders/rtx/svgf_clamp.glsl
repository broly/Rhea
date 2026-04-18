#ifndef SVGF_CLAMP
#define SVGF_CLAMP

struct ClampData
{
    vec3 minC;
    vec3 maxC;
};

ClampData computeClamp(vec2 uv, vec2 texel, int r)
{
    ClampData cd;

    vec3 minC = vec3(1e10);
    vec3 maxC = vec3(-1e10);

    for (int y = -r; y <= r; ++y)
    {
        for (int x = -r; x <= r; ++x)
        {
            vec2 suv = clamp(uv + vec2(x, y) * texel, 0.0, 1.0);

            vec3 c = texture(
                u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_ACCUM],
                suv
            ).rgb;

            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }

    cd.minC = minC;
    cd.maxC = maxC;

    return cd;
}

void computeClampBounds(
    ClampData cd,
    float valid,
    out vec3 minClamp,
    out vec3 maxClamp
)
{
    vec3 mean = 0.5 * (cd.minC + cd.maxC);
    vec3 extent = 0.5 * (cd.maxC - cd.minC);

    float strength = mix(2.5, 1.2, valid);

    minClamp = mean - extent * strength;
    maxClamp = mean + extent * strength;
}

#endif // SVGF_CLAMP