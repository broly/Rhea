#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "resources/mesh_table.glsl"
#include "resources/transform_table.glsl"
#include "resources/pbr_material_ssbo.glsl"
#include "resources/light.glsl"
#include "resources/tlas.glsl"

#include "rtx/ray_payload.glsl"
#include "rtx/rng.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;
layout(location = 1) rayPayloadEXT ShadowPayload shadow_payload;
hitAttributeEXT vec2 attribs;

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
    GPUTransform tr = get_transform(gl_InstanceID);

    vec3 world_pos = (tr.current_transform * vec4(pos, 1.0)).xyz;

    mat3 normal_matrix = transpose(inverse(mat3(tr.current_transform)));
    vec3 N = normalize(normal_matrix * normal);

    // ---------- MATERIAL ----------
    GPUMaterial mat = get_material(mesh_index);
    vec3 albedo = get_base_color(mat, uv).rgb;

    // ================= DIRECT LIGHT =================
    vec3 direct = vec3(0.0);

    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);
        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            // ---------- SHADOW RAY ---------
            shadow_payload.hit = false;

            traceRayEXT(
                u_tlas,
                gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                0, 0, 0,
                world_pos + N * 0.001,
                0.001,
                L,
                10000.0,
                1
            );

            if (!shadow_payload.hit)
            {
                direct = light_ubo.dir_light.color.rgb * NdotL;
            }
        }
    }

    // ---------- ADD LIGHT ----------
    payload.radiance += payload.throughput * direct * albedo;

    // ---------- STOP ----------
    if (payload.depth >= 1)
    return;

    // ================= BOUNCE =================
    uint seed = uint(gl_LaunchIDEXT.x * 1973 + gl_LaunchIDEXT.y * 9277 + gl_PrimitiveID);

    vec3 dir = cosine_sample(N, seed);

    payload.throughput *= albedo;
    payload.depth++;

    traceRayEXT(
        u_tlas,
        gl_RayFlagsOpaqueEXT,
        0xFF,
        1, 1, 1,
        world_pos + N * 0.001,
        0.001,
        dir,
        10000.0,
        0
    );
}