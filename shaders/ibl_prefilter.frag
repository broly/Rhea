#version 450
layout(set = SET_IBL, binding = BINDING_ENV_MAP) uniform samplerCube u_env_map;

layout(location = 0) out vec4 FragColor;

void main() {
    vec3 R = normalize(vec3(gl_FragCoord.xy / 128.0 * 2.0 - 1.0, 1.0));
    vec3 prefiltered = vec3(0.0);
    const uint SAMPLE_COUNT = 1024u;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec3 sample_dir = R;
        prefiltered += texture(u_env_map, sample_dir).rgb;
    }

    prefiltered /= float(SAMPLE_COUNT);
    FragColor = vec4(prefiltered, 1.0);
}