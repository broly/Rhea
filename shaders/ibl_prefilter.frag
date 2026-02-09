#version 450

layout(set = SET_IBL, binding = BINDING_ENV_MAP) uniform samplerCube u_env_map;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Push
{
    uint face_index;
    float roughness;
} pc;

const float PI = 3.14159265359;

// -----------------------------
// Cubemap face + UV → direction
// -----------------------------
vec3 face_uv_to_dir(uint face, vec2 uv)
{
    uv = uv * 2.0 - 1.0;

    switch (face)
    {
        case 0: return normalize(vec3( 1.0, -uv.y, -uv.x));
        case 1: return normalize(vec3(-1.0, -uv.y,  uv.x));
        case 2: return normalize(vec3( uv.x,  1.0,  uv.y));
        case 3: return normalize(vec3( uv.x, -1.0, -uv.y));
        case 4: return normalize(vec3( uv.x, -uv.y,  1.0));
        case 5: return normalize(vec3(-uv.x, -uv.y, -1.0));
    }
    return vec3(0.0);
}

// -----------------------------
// Hammersley
// -----------------------------
float radical_inverse_vdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), radical_inverse_vdc(i));
}

// -----------------------------
// GGX importance sampling
// -----------------------------
vec3 importance_sample_ggx(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(
    cos(phi) * sinTheta,
    sin(phi) * sinTheta,
    cosTheta
    );

    vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// -----------------------------
// Main
// -----------------------------
void main()
{
    vec3 R = face_uv_to_dir(pc.face_index, v_uv);
    vec3 N = R;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;

    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importance_sample_ggx(Xi, N, pc.roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefiltered += texture(u_env_map, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered /= max(totalWeight, 0.001);
    FragColor = vec4(prefiltered, 1.0);
}
