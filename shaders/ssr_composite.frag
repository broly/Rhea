#version 450

#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"
#include "resources/ssr.glsl"

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / textureSize(u_hdr_color[COLOR_OUTPUT_HDR_BASE], 0);

    vec3 base = texture(u_hdr_color[COLOR_OUTPUT_HDR_BASE], uv).rgb;

    vec4 ssr_sample = texture(u_ssr, uv);

    vec3 ssr = ssr_sample.rgb;
    float ssr_alpha = ssr_sample.a;

    float roughness = texture(u_roughness, uv).r;

    float reflectivity = 1.0 - (roughness * 0.5);

    vec3 result = base + ssr * ssr_alpha * 2;

    out_color = vec4(result, 1.0);
}