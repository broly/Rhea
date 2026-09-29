#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"
// wireframe.frag does not read the draw record
#define DRAW_RECORD_NO_VARYING
#include "push_constants/model_push_constants.glsl"
#include "utils/hsv.glsl"

layout(location = 0) out vec4 out_color;

// Mesh edges as a line list without fillModeNonSolid: the draw has 6 vertices per triangle
// (3 edges: 0-1, 1-2, 2-0), each vertex pulls its triangle corner from the index buffer.
// Skinned primitives point at the skinned vertices, so the wires follow the animation.

// Pulls lines towards the eye (screen position is unchanged) so they win the depth test
// against the triangles they were built from.
const float DEPTH_PULL = 0.997;

void main()
{
    GPUPrimitiveInfo primitive_info = get_primitive_info(get_primitive_index());

    const int triangle = gl_VertexIndex / 6;
    const int edge_vertex = gl_VertexIndex % 6;
    const int corner = ((edge_vertex + 1) / 2) % 3;

    Vertex v = fetch_vertex(primitive_info.mesh_id, triangle * 3 + corner);

    vec4 world_pos = primitive_info.current_transform * vec4(v.position, 1.0);
    vec4 view_pos = camera_ubo.view * world_pos;
    view_pos.xyz *= DEPTH_PULL;

    gl_Position = camera_ubo.proj * view_pos;

    // stable per primitive color (golden ratio hue walk)
    const vec3 hue = cyclicHSV(float(get_primitive_index()) * 0.618034);
    out_color = vec4(mix(hue, vec3(1.0), 0.35), 1.0);
}
