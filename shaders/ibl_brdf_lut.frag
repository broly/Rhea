#version 450
layout(location = 0) out vec2 FragColor;

const float PI = 3.14159265359;

vec2 integrateBRDF(float NdotV, float roughness) {
    return vec2(NdotV * 0.5, roughness * 0.5);
}

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(512.0);
    FragColor = integrateBRDF(uv.x, uv.y);
}
