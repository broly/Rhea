#version 450

// Temporal anti-aliasing (pass "TAA", GameRenderGraph): the projection is jittered by a sub-pixel offset every
// frame (Halton 2, 3), so over a few frames every pixel samples its whole area; the resolved frame of the
// previous frame (hdr_color_history[BASE]) is reprojected to this one and blended with the new sample.
// Edges, thin grass blades, alpha tested leaves and shimmering highlights converge to their coverage instead of
// popping between frames.
//
//  - reprojection: the motion vectors of the g-buffer (curr uv - prev uv, both jittered: the jitter delta is
//    taken off), of the nearest surface in 3x3 (edges move with the object in front); the sky by the camera
//  - Catmull-Rom history (5 bilinear taps): bilinear history blurs a little more every frame
//  - variance clipping in YCoCg (Salvi): the history is clipped to mean +- gamma * sigma of the 3x3
//    neighbourhood of the new frame, what it showed but the new frame does not (disocclusion, lighting
//    change) is dropped instead of ghosting
//  - everything in a tone mapped space (c / (1 + max(c))): a firefly of the HDR frame weighs as much as a
//    white pixel, not thousands of them
//
// The default sampler repeats: history taps are clamped into the texture.

#include "resources/camera.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"

layout(push_constant) uniform TAAPushConstants
{
    vec4 jitter;        // xy: this frame's jitter - the previous frame's (uv)
    float feedback;     // share of the new frame in the result
    float gamma;        // width of the variance box, in sigmas
    uint reset;         // 1: no usable history (first frame, resize, TAA switched on)
    uint pad;
} pc;

layout(location = 0) out vec4 out_color;

vec3 tonemap(vec3 c)
{
    return c / (1.0 + max(c.r, max(c.g, c.b)));
}

vec3 untonemap(vec3 c)
{
    return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4);
}

vec3 rgb_to_ycocg(vec3 c)
{
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}

vec3 ycocg_to_rgb(vec3 c)
{
    return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

vec3 history_tap(vec2 uv, vec2 size)
{
    uv = clamp(uv, 0.5 / size, 1.0 - 0.5 / size);
    return tonemap(max(textureLod(u_hdr_color_history[COLOR_OUTPUT_HDR_BASE], uv, 0.0).rgb, vec3(0.0)));
}

// Catmull-Rom filtered history in 5 bilinear taps (the corners of the 4x4 kernel left out)
vec3 sample_history(vec2 uv, vec2 size)
{
    vec2 sample_pos = uv * size;
    vec2 center = floor(sample_pos - 0.5) + 0.5;
    vec2 f = sample_pos - center;

    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);

    vec2 w12 = w1 + w2;
    vec2 uv0 = (center - 1.0) / size;
    vec2 uv3 = (center + 2.0) / size;
    vec2 uv12 = (center + w2 / w12) / size;

    float weights[5] = float[5](w12.x * w0.y, w0.x * w12.y, w12.x * w12.y, w3.x * w12.y, w12.x * w3.y);
    vec3 result = history_tap(vec2(uv12.x, uv0.y), size) * weights[0]
                + history_tap(vec2(uv0.x, uv12.y), size) * weights[1]
                + history_tap(uv12, size) * weights[2]
                + history_tap(vec2(uv3.x, uv12.y), size) * weights[3]
                + history_tap(vec2(uv12.x, uv3.y), size) * weights[4];
    float weight_sum = weights[0] + weights[1] + weights[2] + weights[3] + weights[4];
    // the negative lobes may overshoot next to a bright edge
    return max(result / weight_sum, vec3(0.0));
}

// the history moved towards the center of the box until it is inside (Playdead): keeps its hue, unlike a clamp
vec3 clip_to_box(vec3 history, vec3 box_min, vec3 box_max)
{
    vec3 center = 0.5 * (box_max + box_min);
    vec3 extent = 0.5 * (box_max - box_min) + 1e-5;
    vec3 offset = history - center;
    vec3 units = abs(offset / extent);
    float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0 ? center + offset / largest : history;
}

void main()
{
    const ivec2 size = textureSize(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], 0);
    const ivec2 pixel = min(ivec2(gl_FragCoord.xy), size - 1);
    const vec2 uv = (vec2(pixel) + 0.5) / vec2(size);

    const vec3 current_rgb = texelFetch(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], pixel, 0).rgb;
    if (pc.reset != 0u || any(isnan(current_rgb)) || any(isinf(current_rgb)))
    {
        out_color = vec4(current_rgb, 1.0);
        return;
    }

    // ---- the 3x3 neighbourhood of the new frame: its moments, and the nearest surface ----
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    vec3 current = vec3(0.0);
    float nearest_depth = 2.0;
    ivec2 nearest_pixel = pixel;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            const ivec2 p = clamp(pixel + ivec2(x, y), ivec2(0), size - 1);
            const vec3 c = rgb_to_ycocg(tonemap(max(texelFetch(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], p, 0).rgb, vec3(0.0))));
            if (x == 0 && y == 0)
                current = c;
            m1 += c;
            m2 += c * c;
            // depth 0 near .. 1 far
            const float depth = texelFetch(u_gbuffer[GBUFFER_SLOT_DEPTH], p, 0).r;
            if (depth < nearest_depth)
            {
                nearest_depth = depth;
                nearest_pixel = p;
            }
        }
    const vec3 mean = m1 / 9.0;
    const vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));

    // ---- where this pixel was a frame ago ----
    vec2 velocity;
    if (nearest_depth >= 1.0)
    {
        // the sky: no motion vectors, it moves with the camera's rotation
        vec4 world = camera_ubo.inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec4 prev_clip = camera_ubo.prev_proj * camera_ubo.prev_view * vec4(world.xyz / world.w, 1.0);
        velocity = prev_clip.w > 0.0 ? uv - (prev_clip.xy / prev_clip.w * 0.5 + 0.5) : vec2(0.0);
    }
    else
    {
        velocity = texelFetch(u_gbuffer[GBUFFER_SLOT_MOTION_VECTORS], nearest_pixel, 0).xy;
    }
    velocity -= pc.jitter.xy;
    const vec2 prev_uv = uv - velocity;

    if (any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0))) || any(isnan(prev_uv)))
    {
        out_color = vec4(current_rgb, 1.0);
        return;
    }

    vec3 history = rgb_to_ycocg(sample_history(prev_uv, vec2(size)));
    if (any(isnan(history)) || any(isinf(history)))
    {
        out_color = vec4(current_rgb, 1.0);
        return;
    }
    history = clip_to_box(history, mean - pc.gamma * sigma, mean + pc.gamma * sigma);

    // fast motion: the history is resampled every frame and blurs, the new frame takes a larger share
    const float speed = length(velocity * vec2(size));   // pixels per frame
    const float feedback = mix(pc.feedback, min(pc.feedback * 2.5, 1.0), clamp(speed / 16.0, 0.0, 1.0));

    const vec3 result = mix(history, current, feedback);
    out_color = vec4(untonemap(max(ycocg_to_rgb(result), vec3(0.0))), 1.0);
}
