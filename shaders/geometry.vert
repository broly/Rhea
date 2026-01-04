#version 450
#include "definitions.glsl"

// ---------- Vertex inputs ----------
layout(location = VERT_LOCATION_POSTION) in vec3 in_position;
layout(location = VERT_LOCATION_NORMAL)  in vec3 in_normal;
layout(location = VERT_LOCATION_UV)      in vec2 in_uv;
layout(location = VERT_LOCATION_TANGENT) in vec3 in_tangent;

// ---------- Outputs ----------
layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_world_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec3 v_world_tangent;

// ---------- Camera ----------
layout(set = SET_CAMERA, binding = BINDING_CAMERA_UBO) uniform CameraUBO
{
    mat4 view_proj;
    vec4 camera_pos;
} camera;

// ---------- Push constants ----------
layout(push_constant) uniform PushConstants
{
    mat4 model;
} pc;

void main()
{
    v_uv = in_uv;

    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    v_world_pos = world_pos.xyz;

    mat3 normal_matrix = transpose(inverse(mat3(pc.model)));
    v_world_normal  = normal_matrix * in_normal;
    v_world_tangent = normal_matrix * in_tangent;

    gl_Position = camera.view_proj * world_pos;
}
