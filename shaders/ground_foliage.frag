#version 450

// G-buffer pass of the ground foliage (FoliageRenderer): the "foliage" material of the plant's asset (base color
// with the alpha of the cut out blades and leaves, OpenGL normal map, ORM), tinted per plant. Lit by the foliage
// shading model: g_emissive.rgb carries the light that passes through the leaves (lighting.frag).

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/pbr_material_table.glsl"
#include "character/shading_models.glsl"
#include "foliage/foliage_material.glsl"
#include "foliage/ground_foliage.glsl"

FOLIAGE_VARYINGS(in)

layout(location = 0) out vec4 out_g_world_normal;
layout(location = 1) out vec2 out_g_motion_vectors;
layout(location = 2) out vec4 out_g_albedo_roughness;
layout(location = 3) out float out_g_linear_depth;
layout(location = 4) out vec4 out_g_geometry_normal;
layout(location = 5) out vec4 out_g_emissive;

void main()
{
    const GPUMaterial mat = get_material(v_info.x);

    // cut out blades and leaves (opaque materials have a clip of 0)
    if (mat.params0.z > 0.0 && foliage_opacity(mat, v_uv) < mat.params0.z)
        discard;

    // thin surfaces: lit on the side they are seen from
    vec3 Ng = normalize(v_normal);
    const vec3 V = normalize(camera_ubo.camera_pos.xyz - v_world_pos);
    const float face_sign = dot(Ng, V) < 0.0 ? -1.0 : 1.0;
    Ng *= face_sign;
    const vec3 T = normalize(v_tangent);
    const vec3 B = normalize(v_bitangent) * face_sign;
    vec3 N = normalize(mat3(T, B, Ng) * foliage_normal_ts(mat, v_uv));
    // far plants are lit like the ground they cover
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), v_params.y));

    const vec3 albedo = pow(get_base_color(mat, v_uv).rgb, vec3(2.2)) * v_color;
    const vec3 orm = get_orm(mat, v_uv);
    const float ao = orm.r * v_params.x;
    const float roughness = clamp(orm.g, 0.0, 1.0);

    out_g_world_normal = vec4(N * 0.5 + 0.5, ao);

    const vec2 curr_ndc = v_curr_clip.xy / v_curr_clip.w;
    const vec2 prev_ndc = v_prev_clip.xy / v_prev_clip.w;
    out_g_motion_vectors = (curr_ndc * 0.5 + 0.5) - (prev_ndc * 0.5 + 0.5);

    // the root in the shade of the plant: darker for the direct light too
    out_g_albedo_roughness = vec4(albedo * mix(0.6, 1.0, v_params.x), roughness);

    const vec4 view_pos = camera_ubo.view * vec4(v_world_pos, 1.0);
    out_g_linear_depth = -view_pos.z;

    out_g_geometry_normal = vec4(Ng * 0.5 + 0.5, encode_shading_model(SHADING_MODEL_ID_FOLIAGE));
    out_g_emissive = vec4(albedo * mat.params2.rgb * mat.params2.a * v_params.z, 0.0);
}
