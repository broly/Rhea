#version 450

layout(location = LOCATION_ATTR_POSITION) in vec3 in_position;
layout(location = LOCATION_ATTR_COLOR) in vec4 in_color;

layout(location = 0) out vec4 out_color;

layout(set = SET_CAMERA, binding = BINDING_UBO_CAMERA) uniform CameraUBO
{
    mat4 proj;
    mat4 view;
    vec4 camera_pos;
} camera_ubo;

void main()
{
    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(in_position, 1.0);
    out_color   = in_color;
}
