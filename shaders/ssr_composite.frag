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

// SSR of this pixel. At a fraction of the screen (render.ssr.downscale) a texel holds the ray of one screen
// pixel of its block (ssr.frag): of the 2x2 texels around the pixel only those whose pixel is on the same
// surface count, so a reflection does not bleed across the silhouette of an object (a halo of the character
// on a puddle). Averaged premultiplied by the confidence: a texel without a hit holds no colour.
vec4 sample_ssr(ivec2 pixel, float view_depth, vec3 N)
{
    const ivec2 ssr_size = textureSize(u_ssr, 0);
    const ivec2 screen = textureSize(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], 0);
    if (ssr_size == screen)
        return texelFetch(u_ssr, pixel, 0);

    const vec2 scale = vec2(screen) / vec2(ssr_size);
    const vec2 f = (vec2(pixel) + 0.5) / scale - 0.5;
    const ivec2 base = ivec2(floor(f));
    const vec2 frac = f - floor(f);
    const float depth_tolerance = 0.02 * view_depth + 0.05;

    vec3 color = vec3(0.0);
    float confidence = 0.0;
    float weight_sum = 0.0;
    for (int y = 0; y <= 1; ++y)
        for (int x = 0; x <= 1; ++x)
        {
            const ivec2 texel = clamp(base + ivec2(x, y), ivec2(0), ssr_size - 1);
            // the screen pixel ssr.frag traced for this texel
            const ivec2 source = min(ivec2((vec2(texel) + 0.5) / vec2(ssr_size) * vec2(screen)), screen - 1);
            const float source_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], source, 0).r;
            const vec3 source_normal = texelFetch(u_gbuffer[GBUFFER_SLOT_WORLD_NORMAL], source, 0).xyz * 2.0 - 1.0;

            float weight = (x == 1 ? frac.x : 1.0 - frac.x) * (y == 1 ? frac.y : 1.0 - frac.y);
            weight *= exp(-abs(source_depth - view_depth) / depth_tolerance);
            weight *= pow(clamp(dot(normalize(source_normal), N), 0.0, 1.0), 8.0);

            const vec4 ssr = texelFetch(u_ssr, texel, 0);
            if (any(isnan(ssr)) || any(isinf(ssr)))
                continue;
            const float a = clamp(ssr.a, 0.0, 1.0);
            color += ssr.rgb * a * weight;
            confidence += a * weight;
            weight_sum += weight;
        }

    // none of them on this surface (a thin object at half resolution): the probes
    if (weight_sum < 1e-4 || confidence <= 0.0)
        return vec4(0.0);
    return vec4(color / confidence, confidence / weight_sum);
}

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
        // a non-finite reflection must not reach the frame (it spreads through every later pass): dropped there
        const ivec2 pixel = ivec2(gl_FragCoord.xy);
        const float view_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], pixel, 0).r;
        vec4 ssr = sample_ssr(pixel, view_depth, N);
        radiance = mix(radiance, ssr.rgb * ssr_intensity, ssr.a);
    }

    out_color = vec4(base + weight * radiance, 1.0);
}
