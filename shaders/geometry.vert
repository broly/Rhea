#version 450
#include "definitions.glsl"
#include "layout.glsl"


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

// ---------- Camera ----------
layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) uniform CameraUBO
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

    // --- World position ---
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    v_world_pos = world_pos.xyz;
    

    // --- Normal matrix ---
    mat3 normal_matrix = transpose(inverse(mat3(pc.model)));

    // --- Correct TBN (glTF spec) ---
    vec3 N = normalize(normal_matrix * in_normal);
    vec3 T = normalize(normal_matrix * in_tangent.xyz);
    vec3 B = cross(N, T) * in_tangent.w;

    v_world_normal   = N;
    v_world_tangent  = T;
    v_world_bitangent = B;

    gl_Position = camera.view_proj * world_pos;
    // gl_Position.y = -gl_Position.y;
}
