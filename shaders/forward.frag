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
    
    out_color = vec4(albedo, alpha);

}