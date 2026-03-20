#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "resources/mesh_table.glsl"
#include "resources/primitive_table.glsl"
#include "resources/pbr_material_table.glsl"
#include "resources/light.glsl"
#include "resources/tlas.glsl"

#include "rtx/ray_payload.glsl"
#include "rtx/rng.glsl"

#include "rtx/sbt.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;
layout(location = 1) rayPayloadEXT ShadowPayload shadow_payload;
hitAttributeEXT vec2 attribs;

layout(push_constant) uniform RTXGIPushConstants
{
    uint frame;
    float intensity;
} pc;

void main()
{
    uint mesh_index = gl_InstanceCustomIndexEXT;

    // ---------- TRI ----------
    Vertex v0 = fetch_vertex(mesh_index, 3 * gl_PrimitiveID + 0);
    Vertex v1 = fetch_vertex(mesh_index, 3 * gl_PrimitiveID + 1);
    Vertex v2 = fetch_vertex(mesh_index, 3 * gl_PrimitiveID + 2);

    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    vec3 pos = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    vec3 normal = normalize(
        v0.normal * bary.x +
        v1.normal * bary.y +
        v2.normal * bary.z
    );

    vec2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;

    // ---------- TRANSFORM ----------
    uint primitive_id = gl_InstanceID;
    GPUPrimitiveInfo primitive_info = get_primitive_info(primitive_id);

    vec3 world_pos = (primitive_info.current_transform * vec4(pos, 1.0)).xyz;

    mat3 normal_matrix = transpose(inverse(mat3(primitive_info.current_transform)));
    vec3 N = normalize(normal_matrix * normal);

    // ---------- MATERIAL ----------
    GPUMaterial mat = get_material(primitive_info.material_id);
    vec3 albedo = get_base_color(mat, uv).rgb;
    vec3 emissive = get_emissive(mat, uv).rgb;

    // =====================================================
    // EMISSION
    // =====================================================
    payload.radiance += payload.throughput * emissive;

    // =====================================================
    // NEXT EVENT ESTIMATION
    // =====================================================

    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            shadow_payload.hit = false;

            traceRayEXT(
                u_tlas,
                gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                RTXGI_HIT_PRIMARY,
                RTXGI_DEFAULT_STRIDE,
                RTXGI_MISS_PRIMARY,
                world_pos + N * 0.001,
                0.001,
                L,
                10000.0,
                1
            );

            if (!shadow_payload.hit)
            {
                vec3 light = light_ubo.dir_light.color.rgb;
        
                if (payload.depth > 0)
                {
                    payload.radiance += payload.throughput * albedo * light * NdotL;
                }
            }
        }
    }
    // =====================================================
    // STOP
    // =====================================================
    const int MAX_BOUNCES = 4;

    if (payload.depth >= MAX_BOUNCES)
    return;

    // =====================================================
    // RANDOM
    // =====================================================
    uint seed = wang_hash(
        uint(gl_LaunchIDEXT.x) * 1973u +
        uint(gl_LaunchIDEXT.y) * 9277u +
        uint(payload.depth) * 26699u +
        pc.frame * 374761393u
    );

    vec3 dir = cosine_sample(N, seed);

    // =====================================================
    // THROUGHPUT
    // =====================================================
    payload.throughput *= albedo;

    // =====================================================
    // GI BOOST (умеренный!)
    // =====================================================
    payload.throughput *= 1.5;

    // =====================================================
    // RUSSIAN ROULETTE
    // =====================================================
    if (payload.depth >= 2)
    {
        float p = max(payload.throughput.r,
                      max(payload.throughput.g, payload.throughput.b));

        p = clamp(p, 0.1, 1.0);

        if (rand(seed) > p)
        return;

        payload.throughput /= p;
    }

    payload.depth++;

    // =====================================================
    // NEXT BOUNCE
    // =====================================================
    traceRayEXT(
        u_tlas,
        gl_RayFlagsOpaqueEXT,
        0xFF,
        RTXGI_HIT_SECONDARY,
        RTXGI_DEFAULT_STRIDE,
        RTXGI_MISS_SECONDARY,
        world_pos + N * 0.001,
        0.001,
        dir,
        10000.0,
        0
    );
}