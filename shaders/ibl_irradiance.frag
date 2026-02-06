#version 450
layout(set = SET_IBL, binding = BINDING_ENV_MAP) uniform samplerCube u_env_map;
layout(location = 0) out vec4 FragColor;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vec3(gl_FragCoord.xy / 32.0 * 2.0 - 1.0, 1.0));
    vec3 irradiance = vec3(0.0);

    // Simple diffuse convolution
    for (uint i = 0u; i < 1024u; ++i) {
        vec3 sample_dir = normalize(N + vec3(
        fract(sin(float(i) * 12.9898) * 43758.5453),
        fract(sin(float(i) * 78.233) * 43758.5453),
        fract(sin(float(i) * 39.3467) * 43758.5453)
        ));
        irradiance += texture(u_env_map, sample_dir).rgb * max(dot(N, sample_dir), 0.0);
    }
    irradiance /= 1024.0;
    FragColor = vec4(irradiance, 1.0);
}
