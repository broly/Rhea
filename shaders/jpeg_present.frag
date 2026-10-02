#version 450

// Pass "Present" (GameRenderGraph): the tone mapped frame (ldr_color, u_jpeg_source) to the swapchain, mixed
// with its shakalized copy (u_jpeg_result, chain "ScreenJpeg") by the full screen JPEG damage.

#define JPEG_NO_IMAGES
#include "resources/jpeg.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(u_jpeg_source, texel, 0).rgb;
    const float amount = pc.post.y;
    if (amount > 0.0)
        color = mix(color, texelFetch(u_jpeg_result, texel, 0).rgb, amount);
    out_color = vec4(color, 1.0);
}
