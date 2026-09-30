#version 450

// Reflection probe capture: static opaque geometry into one cube face (ReflectionProbeSystem).

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/probe_capture.glsl"
#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"
#include "resources/draw_list.glsl"

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;

void main()
{
    forward_draw_record();
    // record.user: cube face
    GPUDrawRecord record = get_draw_record();
    Vertex v = fetch_indexed_vertex(record.mesh_id, gl_VertexIndex);
    GPUPrimitiveInfo primitive_info = get_primitive_info(record.primitive_id);

    mat4 world = primitive_info.current_transform;
    vec4 world_pos = world * vec4(v.position, 1.0);

    v_world_pos = world_pos.xyz;
    v_world_normal = normalize(transpose(inverse(mat3(world))) * v.normal);
    v_uv = v.uv;

    gl_Position = probe_capture_ubo.face_view_proj[record.user] * world_pos;
}
