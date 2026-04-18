#ifndef SVGF_BILATERAL
#define SVGF_BILATERAL

#include "svgf_common.glsl"

vec3 bilateralFilter(
    vec2 uv,
    vec2 texel,
    int r,
    SVGFPixelData center,
    vec3 minClamp,
    vec3 maxClamp
)
{
    vec3 sum = vec3(0.0);
    float wsum = 0.0;

    float relax = mix(0.4, 1.0, center.valid);

    for (int y = -r; y <= r; ++y)
    {
        for (int x = -r; x <= r; ++x)
        {
            vec2 suv = clamp(uv + vec2(x, y) * texel, 0.0, 1.0);

            vec3 c = texture(
                u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_ACCUM],
                suv
            ).rgb;

            c = clamp(c, minClamp, maxClamp);

            float l = luminance(c);

            vec3 N = normalize(
                get_gbuffer_WORLD_NORMAL(suv).xyz * 2.0 - 1.0
            );

            float d = get_gbuffer_LINEAR_DEPTH(suv).r;

            float dz = abs(center.depth - d);
            float nd = max(dot(center.normal, N), 0.0);

            float wn = pow(nd, 8.0 * relax);
            float wd = exp(-(dz * dz) * 50.0 * relax);

            float diff = l - center.luminance;
            float wl = exp(-(diff * diff) / (center.sigma * center.sigma + 1e-4));

            float w = max(wn * wd * wl, 1e-4);

            sum += c * w;
            wsum += w;
        }
    }

    return sum / wsum;
}


#endif // SVGF_BILATERAL