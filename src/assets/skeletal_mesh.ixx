module;

#include <json/value.h>

export module assets:skeletal_mesh;

import std.compat;

import glm;
import :asset;
import :mesh;
import dependency_collector;
import rhmath;
import rhobject;

#include "common/type_macros.h"


// Max bone influences per vertex supported by the skinning pipeline
// (glTF JOINTS_0 + JOINTS_1).
export constexpr uint32_t MAX_BONE_INFLUENCES = 8;


// GPU-ready skin data for one vertex (std430 friendly, 48 bytes).
//  joints  - 8 x uint16 joint indices, packed two per component (lo | hi << 16)
//  weights - 8 normalized weights
export struct SkinVertex
{
    glm::uvec4 joints = glm::uvec4(0);
    glm::vec4 weights0 = glm::vec4(0.0f);
    glm::vec4 weights1 = glm::vec4(0.0f);
};
static_assert(sizeof(SkinVertex) == 48);


// One non-zero morph target delta of a vertex (std430 friendly, 32 bytes).
export struct MorphDelta
{
    glm::vec3 position = glm::vec3(0.0f);
    uint32_t target = 0;
    glm::vec3 normal = glm::vec3(0.0f);
    float _pad = 0.0f;
};
static_assert(sizeof(MorphDelta) == 32);


// Sparse morph targets of one primitive, CSR by vertex:
// deltas of vertex i are deltas[offsets[i] .. offsets[i + 1]).
export struct PrimitiveMorphs
{
    std::vector<uint32_t> offsets;
    std::vector<MorphDelta> deltas;

    bool empty() const { return deltas.empty(); }
};


export struct SkeletonBone
{
    std::string name;

    // index into Skeleton::bones, -1 for roots
    int32_t parent = -1;

    // local bind (reference) pose relative to parent bone
    glm::vec3 bind_translation = glm::vec3(0.0f);
    glm::quat bind_rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 bind_scale = glm::vec3(1.0f);

    // mesh space -> bone space in bind pose
    glm::mat4 inverse_bind = glm::mat4(1.0f);

    glm::mat4 bind_local_matrix() const;
};


export struct Skeleton
{
    std::vector<SkeletonBone> bones;

    // bones sorted so that every parent precedes its children
    std::vector<uint32_t> evaluation_order;

    // transform of the (non-joint) parent chain of root bones
    glm::mat4 root_transform = glm::mat4(1.0f);

    std::unordered_map<std::string, uint32_t> bone_by_name;

    uint32_t num_bones() const { return (uint32_t)bones.size(); }

    std::optional<uint32_t> find_bone(const std::string& name) const;

    // local pose of each bone (bind pose by default)
    std::vector<glm::mat4> make_bind_local_pose() const;

    // local pose -> skinning matrices (bone_global * inverse_bind)
    void compute_skinning_matrices(const std::vector<glm::mat4>& local_pose, std::vector<glm::mat4>& out_skinning) const;

    void build_evaluation_order();
};


export struct SkeletalMesh
{
    DEFAULT_NON_COPYABLE(SkeletalMesh)

    static std::optional<SkeletalMesh> create_from_file(const std::filesystem::path& path);

    std::string name;

    // bind-pose geometry, registered in AssetManager as a regular mesh.
    MeshHandle render_mesh;

    Skeleton skeleton;

    // skin data per primitive of render_mesh (geometry 0), parallel to its primitives
    std::vector<std::vector<SkinVertex>> primitive_skins;

    // morph targets (blend shapes), shared by all primitives (glTF mesh targets).
    // Names come from mesh extras "targetNames" (target_<i> when absent).
    std::vector<std::string> morph_target_names;
    std::unordered_map<std::string, uint32_t> morph_target_by_name;

    // parallel to primitive_skins, empty for primitives without non-zero deltas
    std::vector<PrimitiveMorphs> primitive_morphs;

    uint32_t num_morph_targets() const { return (uint32_t)morph_target_names.size(); }
    std::optional<uint32_t> find_morph_target(const std::string& target_name) const;
};


export struct SkeletalMeshHandle : AssetHandle<SkeletalMeshHandle>
{
    const SkeletalMesh& get() const;
};

export void serialize_json_value(SkeletalMeshHandle& target, const Json::Value& value, const SerializationContext& context);
