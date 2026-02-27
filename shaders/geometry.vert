#version 450
#include "definitions.glsl"
#include "resources/camera.glsl"

// ---------- Vertex inputs ----------
layout(location = LOCATION_ATTR_POSITION) in vec3 in_position;
layout(location = LOCATION_ATTR_NORMAL) in vec3 in_normal;
layout(location = LOCATION_ATTR_UV) in vec2 in_uv;
layout(location = LOCATION_ATTR_TANGENT) in vec4 in_tangent;

// ---------- Outputs ----------
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world_tangent;
layout(location = 4) out vec3 v_world_bitangent;
layout(location = 5) out vec4 v_curr_clip;
layout(location = 6) out vec4 v_prev_clip;

// ---------- Push constants ----------
layout(push_constant) uniform ModelPushConstants
{
    mat4 model;
    mat4 prev_model;
} pc;


void main()
{
    v_uv = in_uv;

    vec4 world_curr = pc.model * vec4(in_position, 1.0);
    vec4 world_prev = pc.prev_model * vec4(in_position, 1.0);

    v_world_pos = world_curr.xyz;

    mat3 normal_matrix = transpose(inverse(mat3(pc.model)));

    vec3 N = normalize(normal_matrix * in_normal);
    vec3 T = normalize(normal_matrix * in_tangent.xyz);
    vec3 B = cross(N, T) * in_tangent.w;

    v_world_normal = N;
    v_world_tangent = T;
    v_world_bitangent = B;

    v_curr_clip = (camera_ubo.proj * camera_ubo.view) * world_curr;
    v_prev_clip = (camera_ubo.prev_proj * camera_ubo.prev_view) * world_curr;

    gl_Position = v_curr_clip;
}