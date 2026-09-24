export module assets:skeletal_mesh;

import <filesystem>;
import <string>;
import <vector>;
import <optional>;
import <unordered_map>;
import <json/value.h>;
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
};


export struct SkeletalMeshHandle : AssetHandle<SkeletalMeshHandle>
{
    const SkeletalMesh& get() const;
};

export void serialize_json_value(SkeletalMeshHandle& target, const Json::Value& value, const SerializationContext& context);
