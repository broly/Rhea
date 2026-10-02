#version 450

// Pass "Present" (GameRenderGraph): the tone mapped frame (ldr_color, u_jpeg_source) to the swapchain
//   - mixed with its shakalized copy (u_jpeg_result, chain "ScreenJpeg") by the full screen JPEG damage
//   - the 8 x 8 blocks under the hit mask (u_jpeg_mask_sampled, any channel) replaced by the hit copy
//     (u_jpeg_hit_result, chains "HitJpeg<slot>"): whole blocks, so the spots end in steps like a damaged file

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
    if (pc.post.z > 0.0 && any(greaterThan(texelFetch(u_jpeg_mask_sampled, texel / JPEG_MASK_BLOCK, 0), vec4(0.0))))
        color = texelFetch(u_jpeg_hit_result, texel, 0).rgb;
    out_color = vec4(color, 1.0);
}
