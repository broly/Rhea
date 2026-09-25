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

    // one weight per morph target of the mesh (applied before skinning)
    std::vector<float> morph_weights;

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

    // 0 when the primitive has no morph deltas
    uint64_t morph_offsets_address = 0;
    uint64_t morph_deltas_address = 0;
    uint32_t morph_count = 0;
};


export struct SkinningPushConstants
{
    uint64_t src_vertices;
    uint64_t skin;
    uint64_t bones;
    uint64_t dst_vertices;
    uint32_t vertex_count;
    uint32_t bone_count;
    uint64_t morph_offsets;
    uint64_t morph_deltas;
    uint64_t morph_weights;
    uint32_t morph_count;
    uint32_t _pad;
};
REFLECT_STRUCT_RUNTIME(SkinningPushConstants,
    src_vertices, skin, bones, dst_vertices, vertex_count, bone_count,
    morph_offsets, morph_deltas, morph_weights, morph_count, _pad);

export constexpr uint32_t SKINNING_GROUP_SIZE = 64;
