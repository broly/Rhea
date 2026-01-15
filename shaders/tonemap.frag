#version 450

#include "definitions.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_hdr_color;

//layout(set = 0, binding = 1) uniform HDRSettings
//{
//    float exposure;   // 1.0 = neutral
//} hdr;

// ACES Filmic Tonemap
vec3 ACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) /
                 (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr_color = texture(u_hdr_color, v_uv).rgb;

    // exposure
    hdr_color *= 0.2;

    // tonemap
    vec3 mapped = ACES(hdr_color);

    // gamma
    mapped = pow(mapped, vec3(1.0 / 2.2));

    out_color = vec4(mapped, 1.0);
}
