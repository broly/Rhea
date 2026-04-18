#ifndef SVGF_VALIDATION
#define SVGF_VALIDATION

#include "../resources/camera.glsl"
#include "../resources/gbuffer.glsl"

float compute_valid(vec2 uv)
{
    vec2 motion = get_gbuffer_MOTION_VECTORS(uv).rg;
    vec2 prev_uv = clamp(uv - motion, vec2(0.0), vec2(1.0));
    
    vec3 world_pos = get_gbuffer_POSITION(uv).xyz;
    float depth    = get_gbuffer_LINEAR_DEPTH(uv).r;
    vec3 N         = normalize(get_gbuffer_WORLD_NORMAL(uv).xyz * 2.0 - 1.0);
    float prev_depth = get_gbuffer_hist_LINEAR_DEPTH(prev_uv).r;

    vec3 N_prev = get_gbuffer_hist_WORLD_NORMAL(prev_uv).xyz * 2.0 - 1.0;
    
    float rel = abs(depth - prev_depth) / max(depth, 1e-3);
    float depth_w = exp(-rel * 2.0);

    float Nd = max(dot(N, N_prev), 0.0);
    float normal_w = smoothstep(0.2, 1.0, Nd);

    // reprojection stability
    vec4 curr_clip_reproj = camera_ubo.proj * camera_ubo.view * vec4(world_pos, 1.0);
    vec2 curr_uv_reproj = (curr_clip_reproj.xy / curr_clip_reproj.w) * 0.5 + 0.5;

    float motion_len = length(motion);
    
    float valid = depth_w * normal_w;
    float boost_k = mix(3, 0.6, clamp(motion_len * 50.0, 0.0, 1.0));
    valid = 1.0 - pow(1.0 - valid, boost_k);
    {
        vec2 centered_uv = uv * 2.0 - 1.0;
        float edge_dist = length(centered_uv);
        float vignette = smoothstep(0.6, 1.0, edge_dist);
        float motion_len = length(motion);
        float motion_w = smoothstep(0.001, 0.01, motion_len);
        float near_w = 1.0 - smoothstep(0.1, 1.0, depth / 4);
        float edge_invalid = vignette * motion_w * near_w;
        valid *= (1.0 - edge_invalid);
    }
    
    return valid;
}

#endif  // SVGF_VALIDATION