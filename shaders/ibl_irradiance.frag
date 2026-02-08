#version 450

layout(set = SET_IBL, binding = BINDING_ENV_MAP) uniform samplerCube u_env_map;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 FragColor;

layout(push_constant) uniform Push
{
    uint face_index;
} pc;

const float PI = 3.14159265359;

// -----------------------------
// Cubemap face + UV → direction
// -----------------------------
vec3 face_uv_to_dir(uint face, vec2 uv)
{
    // uv: [0,1] → [-1,1]
    uv = uv * 2.0 - 1.0;

    // Vulkan / OpenGL cubemap convention
    switch (face)
    {
        case 0: return normalize(vec3( 1.0, -uv.y, -uv.x)); // +X
        case 1: return normalize(vec3(-1.0, -uv.y,  uv.x)); // -X
        case 2: return normalize(vec3( uv.x,  1.0,  uv.y)); // +Y
        case 3: return normalize(vec3( uv.x, -1.0, -uv.y)); // -Y
        case 4: return normalize(vec3( uv.x, -uv.y,  1.0)); // +Z
        case 5: return normalize(vec3(-uv.x, -uv.y, -1.0)); // -Z
    }
    return vec3(0.0);
}

// -----------------------------
// Hammersley sequence
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
// Cosine-weighted hemisphere sampling
// -----------------------------
vec3 cosine_sample_hemisphere(vec2 Xi)
{
    float phi = 2.0 * PI * Xi.x;
    float cos_theta = sqrt(1.0 - Xi.y);
    float sin_theta = sqrt(Xi.y);

    return vec3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        cos_theta
    );
}

// -----------------------------
// Tangent space → world space
// -----------------------------
vec3 to_world(vec3 v, vec3 N)
{
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return tangent * v.x + bitangent * v.y + N * v.z;
}

// -----------------------------
// Main
// -----------------------------
void main()
{
    // Normal for this cubemap texel
    vec3 N = face_uv_to_dir(pc.face_index, v_uv);

    vec3 irradiance = vec3(0.0);

    const uint SAMPLE_COUNT = 1024u;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 L = cosine_sample_hemisphere(Xi);
        L = to_world(L, N);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            irradiance += texture(u_env_map, L).rgb * NdotL;
        }
    }

    irradiance *= PI / float(SAMPLE_COUNT);
    FragColor = vec4(irradiance, 1.0);
}
