#version 450

#include "resources/camera.glsl"

layout(location = LOCATION_ATTR_POSITION) 
in vec3 in_position;


// ---------- Push constants ----------
layout(push_constant) 
uniform PushConstants
{
    mat4 model;
} pc;


void main()
{
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = camera_ubo.proj * camera_ubo.view * world_pos;
}
