#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D u_depth;

void main()
{
    float depth = texture(u_depth, v_uv).r;

    // simple visualization
    out_color = vec4(vec3(1.0 - depth), 1.0);
}
