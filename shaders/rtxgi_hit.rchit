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
    // =========================================================
    // hit position
    // =========================================================

    vec3 hitPos =
    gl_WorldRayOriginEXT +
    gl_WorldRayDirectionEXT * gl_HitTEXT;

    // =========================================================
    // mesh lookup
    // =========================================================

    uint meshIndex = gl_InstanceCustomIndexEXT;

    GPUMesh mesh = u_mesh_table.meshes[meshIndex];

    VertexBuffer vertices = VertexBuffer(mesh.vertex_address);
    IndexBuffer  indices  = IndexBuffer(mesh.index_address);

    // =========================================================
    // triangle
    // =========================================================

    uint prim = gl_PrimitiveID;

    uint i0 = indices.indices[prim * 3 + 0];
    uint i1 = indices.indices[prim * 3 + 1];
    uint i2 = indices.indices[prim * 3 + 2];

    Vertex v0 = vertices.vertices[i0];
    Vertex v1 = vertices.vertices[i1];
    Vertex v2 = vertices.vertices[i2];

    // =========================================================
    // barycentric
    // =========================================================

    vec3 bary = vec3(
    1.0 - attribs.x - attribs.y,
    attribs.x,
    attribs.y
    );

    // =========================================================
    // interpolated normal
    // =========================================================

    vec3 N = normalize(
        v0.normal * bary.x +
        v1.normal * bary.y +
        v2.normal * bary.z
    );

    // face forward

    if (dot(N, gl_WorldRayDirectionEXT) > 0.0)
    N = -N;

    // =========================================================
    // interpolated UV
    // =========================================================

    vec2 uv =
    v0.uv * bary.x +
    v1.uv * bary.y +
    v2.uv * bary.z;

    // =========================================================
    // material (временно)
    // =========================================================

    vec3 albedo = vec3(0.8);

    // =========================================================
    // DIRECT LIGHT
    // =========================================================


    if (light_ubo.has_dir_light == 1)
    {
        vec3 L = normalize(-light_ubo.dir_light.direction.xyz);

        float NdotL = max(dot(N, L), 0.0);

        if (NdotL > 0.0)
        {
            vec3 radiance = light_ubo.dir_light.color.rgb;

            payload.radiance +=
                payload.throughput *
                albedo *
                radiance *
                NdotL;
        }
    }

    // =========================================================
    // GI BOUNCE
    // =========================================================

    payload.depth++;

    if (payload.depth >= 3)
    return;

    payload.throughput *= albedo;

    // Russian roulette

    float p = max(
        payload.throughput.r,
        max(payload.throughput.g, payload.throughput.b)
    );

    if (rand(payload.seed) > p)
    return;

    payload.throughput /= p;

    // cosine hemisphere

    vec3 newDir = cosine_sample_hemisphere(N, payload.seed);

    // =========================================================
    // trace bounce
    // =========================================================

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