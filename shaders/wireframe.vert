#version 450

#include "resources/camera.glsl"

layout(location = LOCATION_ATTR_POSITION) in vec3 in_position;
layout(location = LOCATION_ATTR_COLOR) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main()
{
    gl_Position = camera_ubo.proj * camera_ubo.view * vec4(in_position, 1.0);
    out_color   = in_color;
}
