#version 450

// Fog of the frame at a fraction of the screen resolution (SkyRenderer, pass "FogMarch"): one march per texel
// from the eye to what the texel's screen pixel shows (the sky: to the max distance of the fog), its samples
// dithered and blended with the reprojected result of the previous frames. fog.frag lays it over the frame.

#include "resources/camera.glsl"
#include "resources/gbuffer.glsl"
#include "resources/light.glsl"
#include "resources/shadow.glsl"
#include "resources/reflection.glsl"
#include "resources/sky.glsl"
#include "resources/fog_buffers.glsl"
#include "sky/view_ray.glsl"
#include "sky/fog.glsl"

layout(push_constant) uniform FogMarchPushConstants
{
    uint steps;             // samples per ray
    float history_weight;   // share of the reprojected history, 0: none
    uint frame;             // dither pattern of the frame
    uint downscale;         // screen pixels per texel, per axis
} pc;

layout(location = 0) in vec2 v_uv;
// rgb: light the fog scatters towards the eye, a: fraction of what is behind it that gets through
layout(location = 0) out vec4 out_fog;

// Interleaved gradient noise (J. Jimenez): well distributed over neighbors and, shifted per frame, over time
float dither(vec2 pixel, uint frame)
{
    pixel += 5.588238 * float(frame % 64u);
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 screen = textureSize(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], 0);
    ivec2 pixel = fog_texel_pixel(texel, int(pc.downscale), screen);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(screen);

    vec3 origin = camera_ubo.camera_pos.xyz;
    vec3 dir = sky_view_ray(uv);
    vec3 forward = -normalize(camera_ubo.inv_view[2].xyz);

    // distance along the view axis, 0: the sky
    float view_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], pixel, 0).r;
    bool is_sky = view_depth <= 0.0;
    float surface_distance = view_depth / max(dot(dir, forward), 1e-3);
    float t_end = is_sky ? fog_sky_distance(origin, dir) : min(surface_distance, sky_ubo.fog_haze.y);

    float fog_distance;
    vec4 fog = fog_march(origin, dir, t_end, dither(vec2(texel), pc.frame), int(pc.steps), fog_distance);

    // Where this fog was on the screen a frame ago: with the surface behind it (in front of the sky: where
    // the fog itself is). Kept only if the same thing was behind it then - what comes out from behind a
    // nearer object had much less fog in front of it.
    if (pc.history_weight > 0.0)
    {
        vec4 clip = camera_ubo.prev_proj * camera_ubo.prev_view
            * vec4(origin + dir * (is_sky ? fog_distance : surface_distance), 1.0);
        vec2 previous_uv = clip.xy / clip.w * 0.5 + 0.5;
        if (clip.w > 0.0 && all(greaterThanEqual(previous_uv, vec2(0.0))) && all(lessThan(previous_uv, vec2(1.0))))
        {
            float previous_depth = texelFetch(u_gbuffer_hist[GBUFFER_SLOT_LINEAR_DEPTH], ivec2(previous_uv * vec2(screen)), 0).r;
            bool same_surface = is_sky
                ? previous_depth <= 0.0
                : abs(previous_depth - clip.w) < 0.1 * clip.w + 0.05;
            vec4 history = textureLod(u_fog_history, previous_uv, 0.0);
            if (same_surface && !any(isnan(history)) && !any(isinf(history)))
                fog = mix(fog, history, pc.history_weight);
        }
    }

    out_fog = fog;
}
