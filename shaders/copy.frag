#version 450

#include "definitions.glsl"

layout(set = SET_COPY, binding = BINDING_SAMPLER_COPY_SOURCE) 
uniform sampler2D u_source;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(u_source, uv);
}