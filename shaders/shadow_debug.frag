#version 450
layout(location = 0) out vec4 out_color;
layout(set = SET_BASE_COLOR, binding = BINDING_SAMPLER_BASE_COLOR) uniform sampler2D u_base_color;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(2048.0, 2048.0); // match swapchain
    float depth = texture(u_base_color, uv).r;
    out_color = vec4(vec3(depth), 1.0);
}