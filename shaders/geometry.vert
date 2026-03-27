#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "definitions.glsl"
#include "resources/camera.glsl"
#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"

#include "push_constants/model_push_constants.glsl"

// ---------- Outputs ----------
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world_tangent;
layout(location = 4) out vec3 v_world_bitangent;
layout(location = 5) out vec4 v_curr_clip;
layout(location = 6) out vec4 v_prev_clip;


void main()
{
    Vertex v = fetch_vertex(get_mesh_index(), gl_VertexIndex);
    
    uint prim_id = get_primitive_index();
    GPUPrimitiveInfo primitive_info = get_primitive_info(prim_id);
    
    mat4 transform_curr = primitive_info.current_transform;
    mat4 transform_prev = primitive_info.prev_transform;
    
    vec4 world_curr = transform_curr * vec4(v.position, 1);
    vec4 world_prev = transform_prev * vec4(v.position, 1);
    
    v_uv = v.uv;  // in_uv;

    v_world_pos = world_curr.xyz;

    mat3 normal_matrix = transpose(inverse(mat3(transform_curr)));

    vec3 N = normalize(normal_matrix * v.normal);
    vec3 T = normalize(normal_matrix * v.tangent.xyz);
    vec3 B = cross(N, T) * v.tangent.w;

    v_world_normal = N;
    v_world_tangent = T;
    v_world_bitangent = B;

    v_curr_clip = (camera_ubo.proj * camera_ubo.view) * world_curr;
    v_prev_clip = (camera_ubo.prev_proj * camera_ubo.prev_view) * world_prev;

    gl_Position = v_curr_clip;
}