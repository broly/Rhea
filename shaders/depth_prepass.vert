#version 450

layout(location = LOCATION_ATTR_POSITION) in vec3 in_position;

// ---------- Camera ----------
layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    vec4 camera_pos;
} camera_ubo;

// ---------- Push constants ----------
layout(push_constant) uniform PushConstants
{
    mat4 model;
} pc;


void main()
{
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = camera_ubo.proj * camera_ubo.view * world_pos;
}
