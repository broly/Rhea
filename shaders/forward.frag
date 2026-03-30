#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "pbr_helpers.glsl"
#include "math.glsl"
#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/shadow.glsl"
#include "resources/hdr_color_output.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;

layout(location = 0) out vec4 out_color;

void main()
{
    uint material_id = get_material_index();
    GPUMaterial mat = get_material(material_id);

    vec4 base_tx = get_base_color(mat, v_uv);

    float alpha = base_tx.a;
    if (alpha <= 0.001)
        discard;

    vec3 albedo = pow(base_tx.rgb, vec3(2.2));

    vec3 orm = get_orm(mat, v_uv);
    float ao        = orm.r;
    float roughness = orm.g;
    float metallic  = orm.b;

    vec3 Ng = normalize(v_world_normal);
    vec3 N  = Ng;

    vec3 V = normalize(camera_ubo.camera_pos.xyz - v_world_pos);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 Lpos = light_ubo.lights[i].position.xyz;
        vec3 Lcol = light_ubo.lights[i].color.rgb;

        vec3 L = normalize(Lpos - v_world_pos);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL <= 0.0) 
            continue;

        float dist = length(Lpos - v_world_pos);
        float attenuation = 1.0 / (dist * dist + 1.0);

        Lo += albedo / 3.14159265 * Lcol * attenuation * NdotL;
    }

    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            float shadow = shadow_factor(v_world_pos, Ng);
            vec3 radiance = light_ubo.dir_light.color.rgb * shadow;

            Lo += albedo / 3.14159265 * radiance * NdotL;
        }
    }

    // vec2 uv = gl_FragCoord.xy / vec2(camera_ubo.resolution.xy);
    vec2 screen_size = vec2(textureSize(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_FILTERED], 0));
    vec2 uv = gl_FragCoord.xy / screen_size;
    vec3 gi = texture(u_hdr_color_present[COLOR_OUTPUT_HDR_RTXGI_FILTERED], uv).rgb * 2.0;

    vec3 color = Lo + gi * albedo;

    out_color = vec4(color, alpha);
}