#version 450

#include "definitions.glsl"

#include "resources/light.glsl"

layout(location = 0) in vec3 in_position;

layout(push_constant) uniform PC
{
    mat4 model;
} pc;



void main()
{
    gl_Position = light_ubo.dir_light.light_vp * pc.model * vec4(in_position, 1.0);
}