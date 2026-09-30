#version 450

// GBuffer pass of the "foliage" material model (trees: bark is opaque, leaves are masked and two sided).
// Writes the same targets as geometry.frag plus the foliage shading model id; g_emissive.rgb carries the
// color of the light that passes through a leaf (lighting.frag).

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"
#include "character/shading_models.glsl"
#include "foliage/foliage_material.glsl"


// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;
layout(location = 5) in vec4 v_curr_clip;
layout(location = 6) in vec4 v_prev_clip;

// ================== OUTPUT ==================
// view normals and positions are not stored: from the world normal and the linear depth (gbuffer.glsl)
layout(location = 0) out vec4 out_g_world_normal;
layout(location = 1) out vec2 out_g_motion_vectors;
layout(location = 2) out vec4 out_g_albedo_roughness;
layout(location = 3) out float out_g_linear_depth;
layout(location = 4) out vec4 out_g_geometry_normal;
layout(location = 5) out vec4 out_g_emissive;


void main()
{
    GPUMaterial mat = get_material(get_material_index());

    vec3 Ng = normalize(v_world_normal);

#if BLEND_MODE_MASKED
    if (foliage_opacity(mat, v_uv) < mat.params0.z)
        discard;

    // a leaf is lit on the side it is seen from; its winding says nothing (LOD quads, mirrored instances)
    vec3 V = normalize(camera_ubo.camera_pos.xyz - v_world_pos);
    const float face_sign = dot(Ng, V) < 0.0 ? -1.0 : 1.0;
#else
    const float face_sign = gl_FrontFacing ? 1.0 : -1.0;
#endif
    Ng *= face_sign;

    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(v_world_bitangent) * face_sign;
    vec3 N = normalize(mat3(T, B, Ng) * foliage_normal_ts(mat, v_uv));

    vec3 albedo = pow(get_base_color(mat, v_uv).rgb, vec3(2.2));
    vec3 orm = get_orm(mat, v_uv);
    float ao = orm.r;
    float roughness = clamp(orm.g, 0.0, 1.0);
    float metallic = clamp(orm.b, 0.0, 1.0);

    // ---- outputs ----

    out_g_world_normal = vec4(N * 0.5 + 0.5, ao);

    vec2 curr_ndc = v_curr_clip.xy / v_curr_clip.w;
    vec2 prev_ndc = v_prev_clip.xy / v_prev_clip.w;
    out_g_motion_vectors = (curr_ndc * 0.5 + 0.5) - (prev_ndc * 0.5 + 0.5);

    out_g_albedo_roughness = vec4(albedo, roughness);

    vec4 view_pos = camera_ubo.view * vec4(v_world_pos, 1.0);
    out_g_linear_depth = -view_pos.z;

    out_g_geometry_normal = vec4(Ng * 0.5 + 0.5, encode_shading_model(SHADING_MODEL_ID_FOLIAGE));
    out_g_emissive = vec4(albedo * mat.params2.rgb * mat.params2.a, metallic);
}
