#version 450

// Reflection probe: diffuse irradiance of one face of the probe's slot (divided by PI: the lighting
// multiplies it by the albedo). Cosine weighted samples from a blurry mip of the capture.

#include "math.glsl"
#include "resources/probe_capture.glsl"

layout(push_constant) uniform ProbeFacePushConstants
{
    uint face;
    uint mip;
    float roughness;
    uint sample_count;
    float blend;        // output alpha: crossfade weight of the new capture (blend pipelines)
} probe_face;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

float radical_inverse_vdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

void main()
{
    vec3 N = probe_face_uv_to_dir(probe_face.face, v_uv);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    const uint count = max(probe_face.sample_count, 1u);
    // each sample covers ~2PI/count sr: read the mip whose texels are about that large
    const float source_size = probe_capture_ubo.filter_params.x;
    const float max_lod = probe_capture_ubo.filter_params.y - 1.0;
    const float texel_solid_angle = 4.0 * PI / (6.0 * source_size * source_size);
    const float lod = clamp(0.5 * log2(2.0 * PI / float(count) / texel_solid_angle) + 1.0, 0.0, max_lod);

    vec3 sum = vec3(0.0);
    for (uint i = 0u; i < count; ++i)
    {
        vec2 Xi = vec2(float(i) / float(count), radical_inverse_vdc(i));
        float phi = 2.0 * PI * Xi.x;
        float cos_theta = sqrt(1.0 - Xi.y);
        float sin_theta = sqrt(Xi.y);
        vec3 L = tangent * (cos(phi) * sin_theta) + bitangent * (sin(phi) * sin_theta) + N * cos_theta;
        sum += textureLod(u_probe_source, L, lod).rgb;
    }

    // cosine weighted estimator of E / PI
    out_color = vec4(sum / float(count), probe_face.blend);
}
