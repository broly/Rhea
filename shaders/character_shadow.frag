#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/light.glsl"
#include "resources/pbr_material_table.glsl"
#include "push_constants/model_push_constants.glsl"
#include "character/character_material.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_world_normal;

void main()
{
#if BLEND_MODE_MASKED
    GPUMaterial mat = get_material(get_material_index());

    // shadow depth is rendered from the light: the light plays the viewer role
    vec3 V = normalize(-light_ubo.dir_light.direction.xyz);

    if (character_opacity(mat, v_uv, V, normalize(v_world_normal)) < mat.params0.z)
        discard;
#endif
}
