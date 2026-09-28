#version 450

#include "resources/camera.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"


layout(location = 0) in vec2 v_uv;
// rgb: radiance seen along the reflection ray, a: confidence of the hit. No BRDF here: ssr_composite.frag
// weights it like the reflection probes it replaces (env BRDF from the lighting pass)
layout(location = 0) out vec4 out_ssr;

// coarse march + bisection of the first crossing (was up to 200 fine steps: 6+ ms at 2560x1600)
const int   LINEAR_STEPS = 48;
const int   REFINE_STEPS = 6;
const float MAX_DIST    = 100.0;
// below this the reflection hardly shows (specular weight x roughness fade): left to the probes
const float MIN_CONTRIBUTION = 0.015;
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
    // glossy only: rough lobes are left to the prefiltered probes (smooth hand over, no seam)
    const float roughness_fade = 1.0 - smoothstep(0.45, 0.75, roughness);
    if (roughness_fade <= 0.0)
        return;

    // what the lighting pass will multiply the reflection by (env BRDF x specular occlusion): rough
    // dielectrics reflect a few percent, not worth a ray
    vec3 specular_weight = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_SPECULAR_WEIGHT], v_uv).rgb;
    if (max(specular_weight.r, max(specular_weight.g, specular_weight.b)) * roughness_fade < MIN_CONTRIBUTION)
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

    // only the on screen part of the segment is marched (t_screen: where it leaves the screen)
    vec2 t_bounds = max((vec2(1.0) - start_uv) / delta_uv, -start_uv / delta_uv);   // per axis exit
    float t_screen = clamp(min(abs(delta_uv.x) > 1e-6 ? t_bounds.x : 1.0, abs(delta_uv.y) > 1e-6 ? t_bounds.y : 1.0), 0.0, 1.0);
    if (!(t_screen > 0.0))
        return;

    // about one step per 8 pixels of a 1024 wide screen, at most LINEAR_STEPS
    int steps = int(clamp(max_comp * t_screen * 128.0, 4.0, float(LINEAR_STEPS)));

    float prev_t = 0.0;
    for (int i = 1; i <= steps; i++)
    {
        // NDC z is affine in screen space along the segment: linear interpolation is exact
        float t = t_screen * float(i) / float(steps);
        vec2 uv = start_uv + delta_uv * t;

        float scene_depth = get_gbuffer_DEPTH(uv).r;
        float ray_depth = mix(start_ndc.z, end_ndc.z, t);

        if (ray_depth <= scene_depth)
        {
            prev_t = t;
            continue;
        }

        // the ray went behind the depth buffer between prev_t and t: find the crossing
        float lo = prev_t;
        float hi = t;
        for (int j = 0; j < REFINE_STEPS; j++)
        {
            float mid = 0.5 * (lo + hi);
            vec2 mid_uv = start_uv + delta_uv * mid;
            if (mix(start_ndc.z, end_ndc.z, mid) > get_gbuffer_DEPTH(mid_uv).r)
                hi = mid;
            else
                lo = mid;
        }
        t = hi;
        uv = start_uv + delta_uv * t;
        scene_depth = get_gbuffer_DEPTH(uv).r;
        ray_depth = mix(start_ndc.z, end_ndc.z, t);

        // passed behind an object thicker than THICKNESS: no hit, the probes reflect it
        if (ray_depth - scene_depth >= THICKNESS)
            return;

        {
            vec3 hit_color = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], uv).rgb;
            if (!is_finite(hit_color))
                return;

            // unreliable hits fade out towards the probes: near the screen border (the ray may continue
            // off screen), at the end of the ray, and for rays back towards the camera
            vec2 border = min(uv, 1.0 - uv);
            float edge_fade = smoothstep(0.0, 0.08, min(border.x, border.y));
            float distance_fade = 1.0 - smoothstep(0.75, 1.0, t);
            float facing_fade = 1.0 - smoothstep(0.25, 0.75, refl_dir.z);

            out_ssr = vec4(hit_color, roughness_fade * edge_fade * distance_fade * facing_fade);
            return;
        }
    }
}
