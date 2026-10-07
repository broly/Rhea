#version 450

// Pass "Present" (GameRenderGraph): the finished frame (ldr_color, display encoded: the extensions of
// RenderStage::after_tonemap are in it) to the swapchain, with the HUD over it
//   - the crosshair in the middle of the screen (pc.crosshair.x: arm length in px, 0 hidden; game:hud)
//   - the health bar in the bottom left corner (pc.hud; game:hud)

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = SET_PRESENT, binding = BINDING_SAMPLER_PRESENT_SOURCE) uniform sampler2D u_present_source;

// PresentPushConstants (src/game/game_render_graph.ixx)
layout(push_constant) uniform PresentPushConstants
{
    vec4 hud;       // x: health, y: health lagging behind, z: hit flash, w: health bar scale (0 hidden)
    vec4 crosshair; // x: arm length (px), 0 hidden
} pc;

// 1 on the crosshair: four arms around a gap and a dot; p: px from the middle of the screen
float crosshair(vec2 p, float arm, float gap, float half_width)
{
    const vec2 a = abs(p);
    const bool horizontal = a.y <= half_width && a.x >= gap && a.x <= gap + arm;
    const bool vertical = a.x <= half_width && a.y >= gap && a.y <= gap + arm;
    const bool dot = length(p) <= half_width + 0.5;
    return horizontal || vertical || dot ? 1.0 : 0.0;
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
    const vec2 size = vec2(textureSize(u_present_source, 0));
    vec3 color = texelFetch(u_present_source, texel, 0).rgb;

    const float arm = pc.crosshair.x;
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
