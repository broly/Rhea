#version 450

// Pass "Present" (GameRenderGraph): the tone mapped frame (ldr_color, u_jpeg_source) to the swapchain
//   - mixed with its shakalized copy (u_jpeg_result, chain "ScreenJpeg") by the full screen JPEG damage
//   - the 8 x 8 blocks under the hit mask (u_jpeg_mask_sampled, any channel) replaced by the hit copy
//     (u_jpeg_hit_result, chains "HitJpeg<slot>"): whole blocks, so the spots end in steps like a damaged file
//   - the crosshair in the middle of the screen (pc.post.w: arm length in px, 0 hidden; game:hud)

#define JPEG_NO_IMAGES
#include "resources/jpeg.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

// 1 on the crosshair: four arms around a gap and a dot; p: px from the middle of the screen
float crosshair(vec2 p, float arm, float gap, float half_width)
{
    const vec2 a = abs(p);
    const bool horizontal = a.y <= half_width && a.x >= gap && a.x <= gap + arm;
    const bool vertical = a.x <= half_width && a.y >= gap && a.y <= gap + arm;
    const bool dot = length(p) <= half_width + 0.5;
    return horizontal || vertical || dot ? 1.0 : 0.0;
}

void main()
{
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(u_jpeg_source, texel, 0).rgb;
    const float amount = pc.post.y;
    if (amount > 0.0)
        color = mix(color, texelFetch(u_jpeg_result, texel, 0).rgb, amount);
    if (pc.post.z > 0.0 && any(greaterThan(texelFetch(u_jpeg_mask_sampled, texel / JPEG_MASK_BLOCK, 0), vec4(0.0))))
        color = texelFetch(u_jpeg_hit_result, texel, 0).rgb;

    const float arm = pc.post.w;
    if (arm > 0.0)
    {
        const vec2 p = gl_FragCoord.xy - vec2(textureSize(u_jpeg_source, 0)) * 0.5;
        // a dark outline keeps it visible on bright surfaces
        if (crosshair(p, arm, 4.0, 1.0) > 0.0)
            color = vec3(1.0);
        else if (crosshair(p, arm + 1.0, 3.0, 2.0) > 0.0)
            color *= 0.25;
    }
    out_color = vec4(color, 1.0);
}
