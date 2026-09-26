#version 450

// GBuffer pass of the "character" material model (UE master materials port).
// Writes the same targets as geometry.frag plus the shading model id and
// model specific data, consumed by lighting.frag.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"
#include "character/shading_models.glsl"
#include "character/character_material.glsl"


// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;
layout(location = 5) in vec4 v_curr_clip;
layout(location = 6) in vec4 v_prev_clip;

// ================== OUTPUT ==================
layout(location = 0) out vec4 out_g_normal;
layout(location = 1) out vec4 out_g_world_normal;
layout(location = 2) out vec2 out_g_motion_vectors;
layout(location = 3) out vec4 out_g_albedo_roughness;
layout(location = 4) out vec4 out_g_position;
layout(location = 5) out float out_g_linear_depth;
layout(location = 6) out vec4 out_g_geometry_normal;
layout(location = 7) out vec4 out_g_emissive;


void main()
{
    GPUMaterial mat = get_material(get_material_index());

    vec2 uv = v_uv;

    // two sided materials: flip the frame for back faces
    const float face_sign = gl_FrontFacing ? 1.0 : -1.0;
    vec3 Ng = normalize(v_world_normal) * face_sign;
    vec3 V = normalize(camera_ubo.camera_pos.xyz - v_world_pos);

#if BLEND_MODE_MASKED
    if (character_opacity(mat, uv, V, Ng) < mat.params0.z)
        discard;
#endif

    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(v_world_bitangent) * face_sign;
    mat3 TBN = mat3(T, B, Ng);

    vec3 N = normalize(TBN * character_normal_ts(mat, uv));

    vec3 albedo = character_base_color(mat, uv);
    vec3 orm = character_orm(mat, uv);
    float ao = orm.r;
    float roughness = clamp(orm.g, 0.0, 1.0);
    float metallic = clamp(orm.b, 0.0, 1.0);

    // ---- shading model data ----
#if SHADING_MODEL_HAIR
    uint shading_model = SHADING_MODEL_ID_HAIR;
    vec3 hair_tangent = normalize(TBN * character_hair_tangent_ts(mat, uv));
    vec4 model_data = vec4(hair_tangent * 0.5 + 0.5, mat.params8.w);
#elif SHADING_MODEL_SKIN
    uint shading_model = SHADING_MODEL_ID_SKIN;
    vec4 model_data = vec4(mat.params8.rgb, mat.params8.w);
#else
    uint shading_model = SHADING_MODEL_ID_DEFAULT_LIT;
    vec4 model_data = vec4(character_emissive(mat, uv), metallic);
    if ((get_debug_index() & GEOMETRY_DEBUG_ZERO_EMISSIVE) != 0u)
        model_data.rgb = vec3(0.0);
#endif

    // ---- outputs ----
    vec3 N_view = normalize((camera_ubo.view * vec4(N, 0.0)).xyz);

    out_g_normal = vec4(N_view * 0.5 + 0.5, roughness);
    out_g_world_normal = vec4(N * 0.5 + 0.5, ao);

    vec2 curr_ndc = v_curr_clip.xy / v_curr_clip.w;
    vec2 prev_ndc = v_prev_clip.xy / v_prev_clip.w;
    out_g_motion_vectors = (curr_ndc * 0.5 + 0.5) - (prev_ndc * 0.5 + 0.5);

    out_g_albedo_roughness = vec4(albedo, roughness);
    out_g_position = vec4(v_world_pos, 1.0);

    vec4 view_pos = camera_ubo.view * vec4(v_world_pos, 1.0);
    out_g_linear_depth = -view_pos.z;

    out_g_geometry_normal = vec4(Ng * 0.5 + 0.5, encode_shading_model(shading_model));
    out_g_emissive = model_data;
    if ((get_debug_index() & GEOMETRY_DEBUG_SOLID_EMISSIVE) != 0u)
        out_g_emissive = vec4(1.0, 0.0, 0.0, model_data.a);
}
