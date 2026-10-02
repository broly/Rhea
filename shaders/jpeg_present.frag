#version 450

// Pass "Present" (GameRenderGraph): the tone mapped frame (ldr_color, u_jpeg_source) to the swapchain
//   - mixed with its shakalized copy (u_jpeg_result, chain "ScreenJpeg") by the full screen JPEG damage: the
//     share is the larger of the whole screen one (pc.post.y, hit pulse) and the vignette towards the edges
//     (pc.vignette, low health). With pc.vignette.z > 0 the share is decided per block of that many px and
//     dithered (seed pc.vignette.w): the frame breaks into whole blocks, like a damaged file
//   - the 8 x 8 blocks under the hit mask (u_jpeg_mask_sampled, any channel) replaced by the hit copy
//     (u_jpeg_hit_result, chains "HitJpeg<slot>"): whole blocks, so the spots end in steps like a damaged file
//   - red edges (pc.tint, damage pulse)
//   - the crosshair in the middle of the screen (pc.post.w: arm length in px, 0 hidden; game:hud)
//   - the health bar in the bottom left corner (pc.hud; game:hud)

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

// 0..1, stable for a block and seed
float hash(ivec2 p, uint seed)
{
    uint h = uint(p.x) * 0x8da6b343u ^ uint(p.y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h & 0xffffffu) / 16777216.0;
}

// distance from the middle of the screen: 1 at the top and bottom edges, the horizontal axis squeezed by
// sqrt(aspect) (a rounder tunnel than the screen's rectangle; a 16:9 corner is at 1.67)
float edge_distance(vec2 p, vec2 size)
{
    vec2 q = (p - size * 0.5) / (0.5 * size.y);
    q.x /= sqrt(size.x / size.y);
    return length(q);
}

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// the health bar over color; p: px from the top left corner, size: the screen
vec3 health_bar(vec3 color, vec2 p, vec2 size)
{
    const float scale = pc.hud.w * size.y / 1080.0;
    const vec2 bar_size = vec2(260.0, 12.0) * scale;
    const vec2 origin = vec2(40.0 * scale, size.y - 40.0 * scale - bar_size.y);
    const float border = max(round(2.0 * scale), 1.0);
    const vec2 local = p - origin;
    if (any(lessThan(local, vec2(-border))) || any(greaterThan(local, bar_size + border)))
        return color;

    const float health = pc.hud.x;
    const float lag = max(pc.hud.y, health);
    const float flash = pc.hud.z;
    // the frame: dark, white for a moment after a hit
    if (any(lessThan(local, vec2(0.0))) || any(greaterThan(local, bar_size)))
        return mix(vec3(0.02), vec3(1.0), flash);

    // 20 cells with a gap: the bar reads as digital, like the rest of the damage
    const float cells = 20.0;
    const float cell = bar_size.x / cells;
    const float gap = max(round(1.0 * scale), 1.0);
    const float f = local.x / bar_size.x;
    vec3 fill = mix(color * 0.25, vec3(0.03), 0.6);
    if (mod(local.x, cell) >= cell - gap)
        return fill;

    // white while healthy, red when low
    const vec3 healthy = vec3(0.92, 0.93, 0.88);
    const vec3 low = vec3(0.95, 0.12, 0.08);
    const vec3 bar = mix(low, healthy, smoothstep(0.2, 0.55, health));
    if (f <= health)
        fill = mix(bar, vec3(1.0), flash * 0.6);
    else if (f <= lag)
        fill = mix(vec3(0.55, 0.05, 0.04), vec3(1.0, 0.85, 0.8), flash);
    return fill;
}

void main()
{
    const ivec2 texel = ivec2(gl_FragCoord.xy);
    const vec2 size = vec2(textureSize(u_jpeg_source, 0));
    vec3 color = texelFetch(u_jpeg_source, texel, 0).rgb;

    // full screen JPEG damage: the hit pulse everywhere, the low health towards the edges
    const float radius = pc.vignette.x;
    const float block = pc.vignette.z;
    const vec2 at = block > 0.0 ? (floor(gl_FragCoord.xy / block) + 0.5) * block : gl_FragCoord.xy;
    const float vignette = radius < 2.0 ? smoothstep(radius, radius + pc.vignette.y, edge_distance(at, size)) : 0.0;
    float amount = max(pc.post.y, vignette);
    if (block > 0.0 && amount > 0.0 && amount < 1.0)
        amount = hash(ivec2(gl_FragCoord.xy / block), uint(pc.vignette.w)) < amount ? 1.0 : 0.0;
    if (amount > 0.0)
        color = mix(color, texelFetch(u_jpeg_result, texel, 0).rgb, amount);

    if (pc.post.z > 0.0 && any(greaterThan(texelFetch(u_jpeg_mask_sampled, texel / JPEG_MASK_BLOCK, 0), vec4(0.0))))
        color = texelFetch(u_jpeg_hit_result, texel, 0).rgb;

    // red edges: the frame keeps its light and shade under the color
    const float tint = max(pc.tint.r, max(pc.tint.g, pc.tint.b));
    if (tint > 0.0)
    {
        const float edge = smoothstep(pc.tint.w, pc.tint.w + 1.0, edge_distance(gl_FragCoord.xy, size));
        const vec3 red = pc.tint.rgb / tint;
        color = mix(color, red * (0.2 + 0.9 * luma(color)), clamp(edge * edge * tint, 0.0, 1.0));
    }

    const float arm = pc.post.w;
    if (arm > 0.0)
    {
        const vec2 p = gl_FragCoord.xy - size * 0.5;
        // a dark outline keeps it visible on bright surfaces
        if (crosshair(p, arm, 4.0, 1.0) > 0.0)
            color = vec3(1.0);
        else if (crosshair(p, arm + 1.0, 3.0, 2.0) > 0.0)
            color *= 0.25;
    }

    if (pc.hud.w > 0.0)
        color = health_bar(color, gl_FragCoord.xy, size);
    out_color = vec4(color, 1.0);
}
