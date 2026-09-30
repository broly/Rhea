#version 450

// Image based specular of the frame: the lighting pass wrote the specular weight (env BRDF x specular
// occlusion, hdr_color_present[SPECULAR_WEIGHT]); here it multiplies the reflected radiance: SSR where
// the screen space ray hit, the parallax corrected reflection probes everywhere else, blended by the SSR
// confidence. So reflections of off screen / occluded geometry still come from the probes.

#extension GL_EXT_nonuniform_qualifier : enable

#include "resources/camera.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/gbuffer.glsl"
#include "resources/ssr.glsl"
#include "resources/reflection.glsl"
#include "utils/gbuffer_position.glsl"

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / textureSize(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], 0);

    vec3 base = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_BASE], uv).rgb;
    vec3 weight = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_SPECULAR_WEIGHT], uv).rgb;

    float depth = get_gbuffer_DEPTH(uv).r;
    if (depth >= 1.0 || max(weight.r, max(weight.g, weight.b)) <= 0.0)
    {
        out_color = vec4(base, 1.0);
        return;
    }

    vec3 pos = gbuffer_world_position(ivec2(gl_FragCoord.xy));
    vec3 N = normalize(get_gbuffer_WORLD_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 V = normalize(camera_ubo.camera_pos.xyz - pos);
    vec3 R = reflect(-V, N);
    // same clamp as the lighting pass
    float roughness = clamp(get_gbuffer_ALBEDO_ROUGHNESS(uv).a, 0.04, 1.0);

    vec3 radiance = sample_reflection_probes_specular(pos, R, roughness);

    // SSR off (intensity 0): the pass is skipped, u_ssr holds an old frame
    const float ssr_intensity = reflection_probes_ubo.ssr_params.x;
    if (ssr_intensity > 0.0)
    {
        vec4 ssr = texture(u_ssr, uv);
        // a non-finite reflection must not reach the frame (it spreads through every later pass)
        if (!any(isnan(ssr)) && !any(isinf(ssr)))
            radiance = mix(radiance, ssr.rgb * ssr_intensity, clamp(ssr.a, 0.0, 1.0));
    }

    out_color = vec4(base + weight * radiance, 1.0);
}
