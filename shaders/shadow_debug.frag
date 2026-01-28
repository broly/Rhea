#version 450
layout(location = 0) out vec4 out_color;
layout(set = SET_SHADOWDEBUG, binding = BINDING_SHADOW_DEPTH) uniform sampler2D u_shadow_depth;

void main()
{
    vec2 uv = gl_FragCoord.xy / vec2(2048.0, 2048.0); // match swapchain
    float depth = texture(u_shadow_depth, uv).r;
    out_color = vec4(vec3(depth), 1.0);
}