#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"

#include "resources/light.glsl"
#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"
#include "push_constants/model_push_constants.glsl"

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_world_normal;
#if INSTANCE_DISSOLVE
// character_shadow.frag discards the dissolved parts like character.frag
layout(location = 2) out vec3 v_object_pos;
layout(location = 3) flat out float v_dissolve;
#endif

void main()
{
    forward_draw_record();

    Vertex vertex = fetch_indexed_vertex(get_mesh_index(), gl_VertexIndex);

    GPUPrimitiveInfo primitive_info = get_primitive_info(get_primitive_index());
    mat4 transform_curr = primitive_info.current_transform;

    v_uv = vertex.uv;
#if INSTANCE_DISSOLVE
    v_object_pos = vertex.position;
    v_dissolve = primitive_info.tint.a;
#endif
    v_world_normal = normalize(transpose(inverse(mat3(transform_curr))) * vertex.normal);

    // the push constants carry the cascade (GenericRenderGraph::draw_scene_shadow)
    gl_Position = light_ubo.dir_light.cascade_vp[get_debug_index()] * transform_curr * vec4(vertex.position, 1.0);
}
