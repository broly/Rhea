#ifndef RESOURCES_MESH_TABLE
#define RESOURCES_MESH_TABLE


struct Vertex
{
    vec3 position;
    float _pad0;

    vec3 normal;
    float _pad1;

    vec2 uv;
    vec2 _pad2;

    vec4 tangent;
};

struct GPUMesh
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint index_count;
    uint mesh_index;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer IndexBuffer
{
    uint indices[];
};

layout(std430, set=SET_SCENE, binding=BINDING_SSBO_MESH_TABLE)
readonly buffer MeshTable
{
    GPUMesh meshes[];
} u_mesh_table;

#endif  // RESOURCES_MESH_TABLE