export module render:gpu_types;
import <cstdint>;

import glm;

#include "common/reflect_macros.h"


export struct GPUMesh
{
    uint64_t vertex_address;
    uint64_t index_address;
    uint32_t index_count;
    uint32_t mesh_index;
};
REFLECT_STRUCT(GPUMesh,
    vertex_address, index_address, index_count, mesh_index);


export struct GPUPrimitiveInfo
{
    glm::mat4 current_transform;
    glm::mat4 prev_transform;
    uint32_t mesh_id;
    uint32_t material_id;
    uint32_t pad[2];
};
REFLECT_STRUCT(GPUPrimitiveInfo,
    current_transform, prev_transform, mesh_id, material_id);