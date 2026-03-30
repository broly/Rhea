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
#include "resources/reflection.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"
#include "utils/hsv.glsl"


// ================== INPUTS ==================
layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_world_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_world_tangent;
layout(location = 4) in vec3 v_world_bitangent;
layout(location = 5) in vec4 v_curr_clip;
layout(location = 6) in vec4 v_prev_clip;

// ================== OUTPUT ==================
#if !BLEND_MODE_TRANSLUCENT
layout(location = 0) out vec4 out_g_normal;
layout(location = 1) out vec4 out_g_world_normal;
layout(location = 2) out vec2 out_g_motion_vectors;
layout(location = 3) out vec4 out_g_albedo_roughness;
layout(location = 4) out vec3 out_g_position;
layout(location = 5) out float out_g_linear_depth;
layout(location = 6) out vec4 out_g_geometry_normal;
layout(location = 7) out vec3 out_g_emissive;
#endif 


void main()
{
#if !BLEND_MODE_TRANSLUCENT
    uint material_id = get_material_index();
    GPUMaterial mat = get_material(material_id);

    vec4 base_tx = get_base_color(mat, v_uv);

    vec3 albedo = pow(base_tx.rgb, vec3(2.2));
    
    vec3 emissive = get_emissive(mat, v_uv).rgb;

    vec3 orm = get_orm(mat, v_uv);
    float ao        = orm.r;
    float roughness = orm.g;
    float metallic  = orm.b;

    // ---- normals ----
    vec3 Ng = normalize(v_world_normal);
    vec3 N  = Ng;

    vec3 T = normalize(v_world_tangent);
    vec3 B = normalize(v_world_bitangent);
    mat3 TBN = mat3(T, B, Ng);

    vec3 n_tx = get_normal(mat, v_uv).rgb;
    n_tx.y = 1.0 - n_tx.y;
    vec3 n_ts = n_tx * 2.0 - 1.0;

    N = normalize(TBN * n_ts);

    // ---- outputs ----
    vec3 N_view = normalize((camera_ubo.view * vec4(N, 0.0)).xyz);

    out_g_normal = vec4(N_view * 0.5 + 0.5, roughness);
    out_g_world_normal = vec4(N * 0.5 + 0.5, 1.0);

    vec2 curr_ndc = v_curr_clip.xy / v_curr_clip.w;
    vec2 prev_ndc = v_prev_clip.xy / v_prev_clip.w;

    vec2 curr_uv = curr_ndc * 0.5 + 0.5;
    vec2 prev_uv = prev_ndc * 0.5 + 0.5;

    out_g_motion_vectors = curr_uv - prev_uv;

    out_g_albedo_roughness = vec4(albedo, roughness);
    out_g_position = v_world_pos;

    vec4 view_pos = camera_ubo.view * vec4(v_world_pos, 1.0);
    out_g_linear_depth = -view_pos.z;

    out_g_geometry_normal = vec4(Ng * 0.5 + 0.5, 1.0);

    out_g_emissive = emissive;
#endif
}