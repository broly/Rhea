#version 450

#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"
#include "utils/tonemapping.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

const float NEAR = 0.5;
const float FAR = 2000;

vec3 exposure(in vec3 v)
{
    return v * 0.6;
}

vec3 gamma(in vec3 v)
{
    return pow(v, vec3(1.0 / 3.7));
}

void main()
{
    vec3 hdr_color;
    // hdr_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI], v_uv).rgb;
     hdr_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_FILTERED], v_uv).rgb;
    // hdr_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_REPROJECTED], v_uv).aaa;
    // hdr_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], v_uv).rgb ;
    hdr_color = exposure(hdr_color);
    vec3 mapped = ACES(hdr_color);
    mapped = gamma(mapped);
    out_color = vec4(mapped, 1.0);
}
