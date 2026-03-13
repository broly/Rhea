#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "resources/tlas.glsl"
#include "resources/light.glsl"
#include "resources/mesh_table.glsl"

#include "rtx/ray_payload.glsl"
#include "rtx/rng.glsl"
#include "rtx/sampling.glsl"

layout(location = 0) rayPayloadInEXT RayPayload payload;

hitAttributeEXT vec2 attribs;

void main()
{
    // ------------------------------------------------
    // SHADOW RAY
    // ------------------------------------------------

    if (payload.ray_type == 1)
    {
        payload.shadow_hit = true;
        return;
    }

    // ------------------------------------------------
    // hit position
    // ------------------------------------------------

    vec3 hitPos =
    gl_WorldRayOriginEXT +
    gl_WorldRayDirectionEXT * gl_HitTEXT;

    // ------------------------------------------------
    // mesh lookup
    // ------------------------------------------------

    uint meshIndex = gl_InstanceCustomIndexEXT;

    GPUMesh mesh = u_mesh_table.meshes[meshIndex];

    VertexBuffer vertices = VertexBuffer(mesh.vertex_address);
    IndexBuffer  indices  = IndexBuffer(mesh.index_address);

    // ------------------------------------------------
    // triangle
    // ------------------------------------------------

    uint prim = gl_PrimitiveID;

    uint i0 = indices.indices[prim * 3 + 0];
    uint i1 = indices.indices[prim * 3 + 1];
    uint i2 = indices.indices[prim * 3 + 2];

    Vertex v0 = vertices.vertices[i0];
    Vertex v1 = vertices.vertices[i1];
    Vertex v2 = vertices.vertices[i2];

    // ------------------------------------------------
    // barycentric
    // ------------------------------------------------

    vec3 bary = vec3(
    1.0 - attribs.x - attribs.y,
    attribs.x,
    attribs.y
    );

    // ------------------------------------------------
    // normal
    // ------------------------------------------------

    vec3 N = normalize(
        v0.normal * bary.x +
        v1.normal * bary.y +
        v2.normal * bary.z
    );

    if (dot(N, gl_WorldRayDirectionEXT) > 0.0)
    N = -N;

    // ------------------------------------------------
    // material
    // ------------------------------------------------

    vec3 albedo = vec3(0.8);

    // ------------------------------------------------
    // DIRECT LIGHT
    // ------------------------------------------------

    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);

        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            int old_type = payload.ray_type;
            bool old_hit = payload.shadow_hit;

            payload.ray_type = 1;
            payload.shadow_hit = false;

            traceRayEXT(
                u_tlas,
                gl_RayFlagsTerminateOnFirstHitEXT |
                gl_RayFlagsOpaqueEXT,
                0xff,
                0,
                0,
                0,
                hitPos + N * 0.001,
                0.001,
                L,
                10000.0,
                0
            );

            bool occluded = payload.shadow_hit;

            payload.ray_type = old_type;
            payload.shadow_hit = old_hit;

            if (!occluded)
            {
                payload.radiance +=
                payload.throughput *
                albedo *
                light_ubo.dir_light.color.rgb *
                NdotL;
            }
        }
    }

    // ------------------------------------------------
    // GI bounce
    // ------------------------------------------------

    payload.depth++;

    if (payload.depth >= 3)
    return;

    payload.throughput *= albedo;

    float p = max(
        payload.throughput.r,
        max(payload.throughput.g, payload.throughput.b)
    );

    if (rand(payload.seed) > p)
    return;

    payload.throughput /= p;

    vec3 newDir = cosine_sample_hemisphere(N, payload.seed);

    payload.ray_type = 0;

    traceRayEXT(
        u_tlas,
        gl_RayFlagsOpaqueEXT,
        0xff,
        0,
        0,
        0,
        hitPos + N * 0.001,
        0.001,
        newDir,
        10000.0,
        0
    );
}