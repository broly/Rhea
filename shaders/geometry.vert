#version 450

// ---------- Vertex inputs ----------
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec2 in_uv;

// ---------- Outputs to fragment ----------
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_world_pos;
layout(location = 2) out vec3 v_world_normal;

// ---------- Camera ----------
layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 mvp;
} camera;

// ---------- Push constants ----------
layout(push_constant) uniform PushConstants
{
    mat4 model;
} pc;

void main()
{
    v_uv = in_uv;

    // world position
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    v_world_pos = world_pos.xyz;

    // world normal
    mat3 normal_matrix = mat3(pc.model);
    v_world_normal = normalize(normal_matrix * in_normal);

    gl_Position = camera.mvp * world_pos;
}
