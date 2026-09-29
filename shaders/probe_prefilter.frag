#version 450

// Reflection probe: GGX prefiltered specular, one mip (= roughness) of one face of the probe's slot.
// Filtered importance sampling (GPU Gems 3, ch. 20.4): each sample reads the source mip whose texels
// cover the sample's solid angle, so few samples are enough and the sun does not sparkle.

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

vec2 hammersley(uint i, uint count)
{
    return vec2(float(i) / float(count), radical_inverse_vdc(i));
}

// half vector around +Z, alpha = roughness^2
vec3 importance_sample_ggx(vec2 Xi, float alpha)
{
    float phi = 2.0 * PI * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

float d_ggx(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

void main()
{
    vec3 N = probe_face_uv_to_dir(probe_face.face, v_uv);

    // mip 0: the mirror reflection is the capture itself
    if (probe_face.roughness <= 0.001)
    {
        out_color = vec4(textureLod(u_probe_source, N, 0.0).rgb, probe_face.blend);
        return;
    }

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    const float alpha = probe_face.roughness * probe_face.roughness;
    const float source_size = probe_capture_ubo.filter_params.x;
    const float max_lod = probe_capture_ubo.filter_params.y - 1.0;
    const float texel_solid_angle = 4.0 * PI / (6.0 * source_size * source_size);
    const uint count = max(probe_face.sample_count, 1u);

    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < count; ++i)
    {
        vec3 h = importance_sample_ggx(hammersley(i, count), alpha);
        vec3 H = tangent * h.x + bitangent * h.y + N * h.z;
        // V = N: the usual split sum assumption
        vec3 L = 2.0 * dot(N, H) * H - N;
        float NdotL = dot(N, L);
        if (NdotL <= 0.0)
            continue;

        // pdf of L = D * NdotH / (4 * VdotH) = D / 4 with V = N
        float pdf = d_ggx(h.z, alpha) * 0.25;
        float sample_solid_angle = 1.0 / (float(count) * pdf + 1e-4);
        float lod = clamp(0.5 * log2(sample_solid_angle / texel_solid_angle) + 1.0, 0.0, max_lod);

        sum += textureLod(u_probe_source, L, lod).rgb * NdotL;
        weight += NdotL;
    }

    out_color = vec4(sum / max(weight, 1e-4), probe_face.blend);
}
