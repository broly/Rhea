#version 450

#include "resources/camera.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"


layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_ssr;

const int   MAX_STEPS   = 200;
const float MAX_DIST    = 100.0;
const float THICKNESS   = 0.002;
const float SELF_BIAS   = 0.05;

// Vulkan depth range [0, 1] (glm::perspectiveRH_ZO): NDC z is the depth itself
vec3 reconstruct_view_pos(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = inverse(camera_ubo.proj) * clip;
    return view.xyz / view.w;
}

bool is_finite(vec3 v)
{
    return !any(isnan(v)) && !any(isinf(v));
}

void main()
{
    out_ssr = vec4(0.0);

    float depth = get_gbuffer_DEPTH(v_uv).r;
    if (depth >= 1.0)
        return;

    vec4 normal_data = get_gbuffer_NORMAL(v_uv);
    vec3 normal = normalize(normal_data.xyz * 2.0 - 1.0);
    float roughness = normal_data.w;
    if (roughness > 0.8)
        return;

    vec3 view_pos = reconstruct_view_pos(v_uv, depth);
    vec3 incident = normalize(view_pos);                  // camera -> surface
    vec3 refl_dir = normalize(reflect(incident, normal));

    // The ray must stay in front of the near plane: behind it the projection divides by w <= 0
    // (infinite / NaN screen positions, runaway loops). View space looks down -Z.
    float near_z = reconstruct_view_pos(vec2(0.5), 0.0).z;
    float ray_length = MAX_DIST;
    if (refl_dir.z > 1e-4)
        ray_length = min(ray_length, (near_z - view_pos.z) / refl_dir.z);
    if (!(ray_length > SELF_BIAS * 2.0))
        return;

    vec3 start_vs = view_pos + refl_dir * SELF_BIAS;
    vec3 end_vs   = view_pos + refl_dir * ray_length;

    vec4 start_clip = camera_ubo.proj * vec4(start_vs, 1.0);
    vec4 end_clip   = camera_ubo.proj * vec4(end_vs, 1.0);
    if (!(start_clip.w > 1e-5) || !(end_clip.w > 1e-5))
        return;

    vec3 start_ndc = start_clip.xyz / start_clip.w;
    vec3 end_ndc   = end_clip.xyz / end_clip.w;

    vec2 start_uv = start_ndc.xy * 0.5 + 0.5;
    vec2 end_uv   = end_ndc.xy * 0.5 + 0.5;
    vec2 delta_uv = end_uv - start_uv;

    // also false for NaN
    float max_comp = max(abs(delta_uv.x), abs(delta_uv.y));
    if (!(max_comp > 1e-5))
        return;

    int steps = int(clamp(max_comp * 1024.0, 1.0, float(MAX_STEPS)));

    for (int i = 1; i <= steps; i++)
    {
        // NDC z is affine in screen space along the segment: linear interpolation is exact
        float t = float(i) / float(steps);
        vec2 uv = start_uv + delta_uv * t;

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;

        float scene_depth = get_gbuffer_DEPTH(uv).r;
        float ray_depth = mix(start_ndc.z, end_ndc.z, t);

        if (ray_depth > scene_depth && ray_depth - scene_depth < THICKNESS)
        {
            vec3 hit_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], uv).rgb;
            if (!is_finite(hit_color))
                return;

            float NdotV = max(dot(normal, -incident), 0.0);
            float fresnel = pow(1.0 - NdotV, 5.0);
            float strength = (1.0 - roughness) * fresnel;

            out_ssr = vec4(hit_color * strength, 1.0);
            return;
        }
    }
}
