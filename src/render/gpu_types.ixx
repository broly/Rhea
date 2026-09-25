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
};
