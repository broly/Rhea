#version 450

// Fog over the frame (SkyRenderer, pass "Fog"): the fog marched at low resolution (fog_march.frag) upsampled
// and blended over the lit scene and the sky. Every texel of the fog buffer holds the fog in front of one
// depth; a pixel takes it from the texels around it whose depth is like its own, so the fog does not bleed
// over the edges of near objects.

#include "resources/gbuffer.glsl"
#include "resources/fog_buffers.glsl"

layout(push_constant) uniform FogPushConstants
{
    uint downscale;     // screen pixels per texel of the fog buffer, per axis
} pc;

layout(location = 0) in vec2 v_uv;
// blended: color = rgb * a + frame * (1 - a)
layout(location = 0) out vec4 out_color;

// view depth the sky stands for: further than anything
const float SKY_DEPTH = 1.0e5;

float view_depth(ivec2 pixel)
{
    float depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], pixel, 0).r;
    return depth > 0.0 ? depth : SKY_DEPTH;
}

void main()
{
    ivec2 screen = textureSize(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], 0);
    ivec2 size = textureSize(u_fog, 0);
    int scale = int(pc.downscale);

    float depth = view_depth(min(ivec2(gl_FragCoord.xy), screen - 1));

    // position among the texels, by the pixels they were marched for
    vec2 position = (gl_FragCoord.xy - (float(scale / 2) + 0.5)) / float(scale);
    ivec2 base = ivec2(floor(position));
    vec2 f = position - vec2(base);

    vec4 sum = vec4(0.0);
    float weight_sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        ivec2 offset = ivec2(i & 1, i >> 1);
        ivec2 texel = clamp(base + offset, ivec2(0), size - 1);
        float texel_depth = view_depth(fog_texel_pixel(texel, scale, screen));
        float difference = abs(depth - texel_depth) / min(depth, texel_depth);
        float weight = (offset.x == 0 ? 1.0 - f.x : f.x) * (offset.y == 0 ? 1.0 - f.y : f.y)
            / (0.01 + 100.0 * difference * difference);
        sum += texelFetch(u_fog, texel, 0) * (weight + 1e-6);
        weight_sum += weight + 1e-6;
    }
    vec4 fog = sum / weight_sum;

    float opacity = clamp(1.0 - fog.a, 0.0, 1.0);
    out_color = vec4(fog.rgb / max(opacity, 1e-4), opacity);
}
