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

#include "push_constants/rtxgi_push_constants.glsl"

#include "rtx/ray_payload.glsl"
#include "rtx/rng.glsl"

#include "rtx/sbt.glsl"


#if 0
    // stub
    #define rayPayloadInEXT
    #define rayPayloadEXT
    #define hitAttributeEXT
#endif

layout(location = RTXGI_RAY_PAYLOAD_RAY) 
rayPayloadInEXT RayPayload payload;

layout(location = RTXGI_RAY_PAYLOAD_SHADOW) 
rayPayloadEXT ShadowPayload shadow_payload;

hitAttributeEXT 
vec2 attribs;

void main()
{
    uint prim_id = gl_InstanceCustomIndexEXT;
    GPUPrimitiveInfo primitive_info = get_primitive_info(prim_id);
    uint mesh_index = primitive_info.mesh_id;

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


    vec3 world_pos = (primitive_info.current_transform * vec4(pos, 1.0)).xyz;

    mat3 normal_matrix = transpose(inverse(mat3(primitive_info.current_transform)));
    vec3 N = normalize(normal_matrix * normal);

    // ---------- MATERIAL ----------
    GPUMaterial mat = get_material(primitive_info.material_id);
    vec3 albedo = get_base_color(mat, uv).rgb;
    vec3 emissive = get_emissive(mat, uv).rgb * 25;

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
                RTXGI_RAY_PAYLOAD_SHADOW
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
    
    const int MAX_BOUNCES = 4;

    if (payload.depth >= MAX_BOUNCES)
        return;


    vec3 dir = cosine_sample(N, payload.rng_state);

    payload.throughput *= albedo;

    // payload.throughput *= 1.5;

    if (payload.depth >= 2)
    {
        float p = max(payload.throughput.r,
                      max(payload.throughput.g, payload.throughput.b));

        p = clamp(p, 0.1, 1.0);

        if (rng_state_rand(payload.rng_state) > p)
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
        RTXGI_RAY_PAYLOAD_RAY
    );

}