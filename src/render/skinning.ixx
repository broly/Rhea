export module render:skinning;

import <cstdint>;
import <vector>;
import glm;
import assets;

#include "common/reflect_macros.h"


// Pose of a skeletal mesh instance, shared between the game side (writer)
// and the render side (reader). Bumping `version` triggers GPU re-skinning.
export struct SkinningPose
{
    SkeletalMeshHandle mesh;

    // bone_global * inverse_bind for every bone of the skeleton
    std::vector<glm::mat4> skinning_matrices;

    uint64_t version = 0;
};


// GPU objects of one skinned primitive instance (bind-pose source + skinned output).
export struct SkinnedMeshGPU
{
    uint32_t instance_id = 0;

    // mesh table entry which points to the skinned vertices + source indices
    uint32_t mesh_index = 0;

    uint64_t src_vertex_address = 0;
    uint64_t skin_address = 0;
    uint64_t dst_vertex_address = 0;

    uint32_t vertex_count = 0;
    uint32_t bone_count = 0;
};


export struct SkinningPushConstants
{
    uint64_t src_vertices;
    uint64_t skin;
    uint64_t bones;
    uint64_t dst_vertices;
    uint32_t vertex_count;
    uint32_t bone_count;
};
REFLECT_STRUCT_RUNTIME(SkinningPushConstants,
    src_vertices, skin, bones, dst_vertices, vertex_count, bone_count);

export constexpr uint32_t SKINNING_GROUP_SIZE = 64;
