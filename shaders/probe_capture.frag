#version 450

// Reflection probe capture shading: cheap forward lighting of the static scene, 128^2 per face.
// Diffuse only (the probe stores what the surfaces send towards the capture point, view dependent
// highlights would be wrong from any other point anyway): sun with the shadow map, point lights,
// emissive, and the ambient from the probes themselves - rebaking a probe bounces light once more.
// Only lights with Light::visible_in_reflection_probes (probe_capture_ubo lists them).

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "math.glsl"
#include "resources/probe_capture.glsl"
#include "resources/light.glsl"
#include "resources/shadow.glsl"
#include "resources/reflection.glsl"
#include "resources/pbr_material_table.glsl"
#include "resources/draw_list.glsl"

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

void main()
{
    GPUMaterial mat = get_material(get_draw_record().material_id);

    vec3 albedo = pow(get_base_color(mat, v_uv).rgb, vec3(2.2));
    vec3 orm = get_orm(mat, v_uv);
    float ao = orm.r;
    float metallic = orm.b;
    vec3 emissive = get_emissive(mat, v_uv).rgb;

    // two sided: the capture point may look at back faces (thin walls, foliage)
    vec3 N = normalize(v_world_normal);
    vec3 to_capture = probe_capture_ubo.capture_position.xyz - v_world_pos;
    if (dot(N, to_capture) < 0.0)
        N = -N;

    vec3 diffuse_albedo = albedo * (1.0 - metallic);
    vec3 radiance = vec3(0.0);

    // the sun of the main view (light_ubo, shadow map) if it is visible to probes
    if (light_ubo.has_dir_light == 1 && probe_capture_ubo.light_info.y != 0u)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
            radiance += diffuse_albedo / PI * light_ubo.dir_light.color.rgb * shadow_factor(v_world_pos, N) * NdotL;
    }

    // static point lights near the probe (light_ubo holds the ones near the camera, dynamic included)
    for (uint i = 0u; i < min(probe_capture_ubo.light_info.x, 8u); ++i)
    {
        vec3 to_light = probe_capture_ubo.light_position[i].xyz - v_world_pos;
        float dist = length(to_light);
        float NdotL = max(dot(N, to_light / max(dist, 1e-4)), 0.0);
        radiance += diffuse_albedo / PI * probe_capture_ubo.light_color[i].rgb * NdotL / (dist * dist + 1.0);
    }

    // indirect: what the probes baked so far (sky ambient until the first bake)
    radiance += diffuse_albedo * sample_reflection_probes_irradiance(v_world_pos, N) * ao;
    radiance += emissive;

    out_color = vec4(radiance, 1.0);
}
