#ifndef RESOURCES_MESH_TABLE
#define RESOURCES_MESH_TABLE

#ifndef SET_SCENE
    #define SET_SCENE 0
    #error "SET_SCENE must be provided"
#endif
#ifndef BINDING_SSBO_MESH_TABLE
    #define BINDING_SSBO_MESH_TABLE 0
    #error "BINDING_SSBO_MESH_TABLE must be provided"
#endif


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

layout(buffer_reference, std430, buffer_reference_align = 8) 
readonly buffer VertexBuffer
{
    Vertex vertices[];
};

layout(buffer_reference, std430, buffer_reference_align = 8) 
readonly buffer IndexBuffer
{
    uint indices[];
};

layout(std430, set=SET_SCENE, binding=BINDING_SSBO_MESH_TABLE)
readonly buffer MeshTable
{
    GPUMesh meshes[];
} u_mesh_table;

Vertex fetch_vertex(uint mesh_index, int vertex_index)
{
    GPUMesh mesh = u_mesh_table.meshes[nonuniformEXT(mesh_index)];
    VertexBuffer vb = VertexBuffer(mesh.vertex_address);
    IndexBuffer ib = IndexBuffer(mesh.index_address);
    uint index = ib.indices[vertex_index];
    Vertex v = vb.vertices[index];
    
    return v;
}

#endif  // RESOURCES_MESH_TABLE