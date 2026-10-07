export module render:gpu_types;

import std.compat;
import fixed_string;
import reflect;
import type_id;

import glm;



export struct GPUMesh
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint32_t index_count;
    uint32_t mesh_index;
};


export struct GPUPrimitiveInfo
{
    glm::mat4 current_transform;
    glm::mat4 prev_transform;
    uint32_t mesh_id;
    uint32_t material_id;
    [[=rh::padding]] uint32_t pad[2];
    // per instance effect (MeshRenderer::effect): rgb linear emission added on top (hit flash), a: JPEG mask level
    glm::vec4 effect{ 0.0f };
    // rgb: multiplies the base color (MeshRenderer::tint), a: dissolved share 0..1 (MeshRenderer::dissolve)
    glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 0.0f };
    // rgb: linear emission of the dissolve edge, a: its width (share of the noise range)
    glm::vec4 dissolve_edge{ 0.0f };
};
