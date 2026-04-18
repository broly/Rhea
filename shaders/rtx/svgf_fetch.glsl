#ifndef SVGF_FETCH
#define SVGF_FETCH

#include "svgf_common.glsl"
#include "../resources/hdr_color_output.glsl"
#include "../resources/gbuffer.glsl"


SVGFPixelData svgf_pixel_data_fetch(vec2 uv)
{
    SVGFPixelData p;

    vec4 accum = texture(
        u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_ACCUM],
        uv
    );

    p.color = accum.rgb;

    float w = accum.a;
    p.weight = clamp(w / 32.0, 0.0, 1.0);
    p.valid  = p.weight;

    p.luminance = luminance(p.color);

    vec2 m = texture(
        u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_MOMENTS],
        uv
    ).rg;

    float variance = max(m.y - m.x * m.x, 0.0);
    float min_sigma = mix(0.5, 0.05, p.valid);
    p.sigma = max(sqrt(variance), min_sigma);

    p.normal = normalize(
        get_gbuffer_WORLD_NORMAL(uv).xyz * 2.0 - 1.0
    );

    p.depth = get_gbuffer_LINEAR_DEPTH(uv).r;

    return p;
}

#endif  // SVGF_FETCH
