#ifndef SVGF_VALIDATION
#define SVGF_VALIDATION

#include "../resources/camera.glsl"

float camera_rotation_angle()
{
    vec3 fwd_curr = -vec3(camera_ubo.view[0][2],
                           camera_ubo.view[1][2],
                           camera_ubo.view[2][2]);

    vec3 fwd_prev = -vec3(camera_ubo.prev_view[0][2],
                           camera_ubo.prev_view[1][2],
                           camera_ubo.prev_view[2][2]);

    float c = clamp(dot(fwd_curr, fwd_prev), -1.0, 1.0);
    return acos(c);
}

float compute_weight_cap()
{
    float t = smoothstep(0.017, 0.262, camera_rotation_angle());
    return mix(64.0, 1.0, t);
}

float compute_valid(vec2 uv)
{
    vec2 motion      = get_gbuffer_MOTION_VECTORS(uv).rg;
    vec2 prev_uv_raw = uv - motion;
    vec2 prev_uv     = clamp(prev_uv_raw, vec2(0.0), vec2(1.0));

    // Out-of-screen penalty.
    vec2  oob     = abs(prev_uv_raw - clamp(prev_uv_raw, 0.0, 1.0));
    float oob_w   = 1.0 - smoothstep(0.0, 0.005, max(oob.x, oob.y));

    float depth      = get_gbuffer_LINEAR_DEPTH(uv).r;
    vec3  N          = normalize(get_gbuffer_WORLD_NORMAL(uv).xyz * 2.0 - 1.0);

    float prev_depth = get_gbuffer_hist_LINEAR_DEPTH(prev_uv).r;
    vec3  N_prev     = normalize(get_gbuffer_hist_WORLD_NORMAL(prev_uv).xyz * 2.0 - 1.0);

    float rel     = abs(depth - prev_depth) / max(depth, 1e-3);
    float depth_w = exp(-rel * 4.0);

    float Nd       = max(dot(N, N_prev), 0.0);
    float normal_w = smoothstep(0.5, 1.0, Nd);

    float valid = depth_w * normal_w * oob_w;

    float motion_len = length(motion);
    float boost_k    = mix(3.0, 0.5, smoothstep(0.0, 0.05, motion_len));
    valid = 1.0 - pow(1.0 - valid, boost_k);

    return clamp(valid, 0.0, 1.0);
}

#endif  // SVGF_VALIDATION
