#version 450

#include "resources/gbuffer.glsl"
#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/shadow.glsl"
#include "pbr_helpers.glsl"

layout(location = 0) out vec4 out_color;
layout(location = 0) in vec2 v_uv;

void main()
{
    vec2 uv = v_uv;

    vec3 albedo = get_gbuffer_ALBEDO(uv).rgb;
    vec3 N  = normalize(get_gbuffer_WORLD_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 Ng = normalize(get_gbuffer_GEOMETRY_NORMAL(uv).rgb * 2.0 - 1.0);
    vec3 pos = get_gbuffer_POSITION(uv).rgb;

    vec3 V = normalize(camera_ubo.camera_pos.xyz - pos);

    vec3 direct = vec3(0.0);

    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - pos);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL <= 0.0) 
            continue;

        float dist = length(Lpos - pos);
        float attenuation = 1.0 / (dist * dist + 1.0);

        direct += albedo / 3.14159265 * Lcol * attenuation * NdotL;
    }
    
    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            float shadow = shadow_factor(pos, Ng);
            vec3 radiance = light_ubo.dir_light.color.rgb * shadow;

            direct += albedo / 3.14159265 * radiance * NdotL;
        }
    }

    vec3 gi = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_FILTERED], uv).rgb;
    vec3 indirect = gi * albedo;

    vec3 color = direct + (indirect * 2.0);

    out_color = vec4(color, 1.0);
}