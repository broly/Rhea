#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"

#include "resources/light.glsl"
#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"
// shadow.frag does not read the draw record
#define DRAW_RECORD_NO_VARYING
#include "push_constants/model_push_constants.glsl"

void main()
{
    forward_draw_record();

    Vertex vertex = fetch_vertex(get_mesh_index(), gl_VertexIndex);

    uint prim_id = get_primitive_index();
    GPUPrimitiveInfo primitive_info = get_primitive_info(prim_id);
    mat4 transform_curr = primitive_info.current_transform;
    
    // the push constants carry the cascade (GenericRenderGraph::draw_scene_shadow)
    gl_Position = light_ubo.dir_light.cascade_vp[get_debug_index()] * transform_curr * vec4(vertex.position, 1.0);
}