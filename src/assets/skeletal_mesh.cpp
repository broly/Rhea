module;

module assets:skeletal_mesh;

import <iostream>;
import <array>;
import <cmath>;
import <algorithm>;
import <functional>;
import glm;
import <json/value.h>;

import dependency_collector;
import fastgltf;
import fastgltf_helper;
import log;
import :asset_manager;
import :helpers;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogSkeletalMesh, Log);


glm::mat4 SkeletonBone::bind_local_matrix() const
{
    return glm::translate(glm::mat4(1.0f), bind_translation) *
        glm::mat4_cast(bind_rotation) *
        glm::scale(glm::mat4(1.0f), bind_scale);
}

std::optional<uint32_t> Skeleton::find_bone(const std::string& name) const
{
    auto it = bone_by_name.find(name);
    if (it == bone_by_name.end())
        return std::nullopt;
    return it->second;
}

std::vector<glm::mat4> Skeleton::make_bind_local_pose() const
{
    std::vector<glm::mat4> pose;
    pose.reserve(bones.size());
    for (const auto& bone : bones)
        pose.push_back(bone.bind_local_matrix());
    return pose;
}

void Skeleton::compute_skinning_matrices(const std::vector<glm::mat4>& local_pose, std::vector<glm::mat4>& out_skinning) const
{
    checkf(local_pose.size() == bones.size(), "Local pose size mismatch");

    std::vector<glm::mat4> global(bones.size());
    out_skinning.resize(bones.size());

    for (uint32_t bone_index : evaluation_order)
    {
        const SkeletonBone& bone = bones[bone_index];
        const glm::mat4& parent_global = bone.parent >= 0 ? global[bone.parent] : root_transform;
        global[bone_index] = parent_global * local_pose[bone_index];
        out_skinning[bone_index] = global[bone_index] * bone.inverse_bind;
    }
}

void Skeleton::build_evaluation_order()
{
    evaluation_order.clear();
    evaluation_order.reserve(bones.size());

    std::vector<uint8_t> visited(bones.size(), 0);

    std::function<void(uint32_t)> visit = [&](uint32_t index)
    {
        if (visited[index])
            return;
        visited[index] = 1;
        if (bones[index].parent >= 0)
            visit((uint32_t)bones[index].parent);
        evaluation_order.push_back(index);
    };

    for (uint32_t i = 0; i < bones.size(); ++i)
        visit(i);
}


static void get_node_trs(const fastgltf::Node& node, glm::vec3& t, glm::quat& r, glm::vec3& s)
{
    if (std::holds_alternative<fastgltf::TRS>(node.transform))
    {
        const auto& trs = std::get<fastgltf::TRS>(node.transform);
        t = fastgltf_to_glm(trs.translation);
        r = fastgltf_to_glm(trs.rotation);
        s = fastgltf_to_glm(trs.scale);
        return;
    }

    glm::mat4 m = fastgltf_to_glm(std::get<fastgltf::math::fmat4x4>(node.transform));
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, s, r, t, skew, perspective);
}

static glm::mat4 get_node_matrix(const fastgltf::Node& node)
{
    glm::vec3 t, s;
    glm::quat r;
    get_node_trs(node, t, r, s);
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}


static uint32_t pack_joints(uint32_t a, uint32_t b)
{
    checkf(a <= 0xFFFF && b <= 0xFFFF, "Joint index overflow");
    return a | (b << 16);
}


std::optional<SkeletalMesh> SkeletalMesh::create_from_file(const std::filesystem::path& path)
{
    static constexpr auto supported_extensions =
        fastgltf::Extensions::KHR_mesh_quantization |
        fastgltf::Extensions::KHR_texture_transform |
        fastgltf::Extensions::KHR_materials_variants |
        fastgltf::Extensions::KHR_materials_specular |
        fastgltf::Extensions::KHR_materials_ior;

    fastgltf::Parser parser(supported_extensions);

    constexpr auto gltf_options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    auto gltf_file = fastgltf::MappedGltfFile::FromPath(path);
    if (!gltf_file)
    {
        LogSkeletalMesh.Log("Failed to open glTF file %s", path.string().c_str());
        return std::nullopt;
    }

    auto asset_result = parser.loadGltf(gltf_file.get(), path.parent_path(), gltf_options);
    if (asset_result.error() != fastgltf::Error::None)
    {
        LogSkeletalMesh.Log("Failed to load glTF %s: error %d", path.string().c_str(),
            (int)asset_result.error());
        return std::nullopt;
    }

    fastgltf::Asset& asset = asset_result.get();

    // ---------------------------------------------------------------
    // Find the skinned mesh node
    // ---------------------------------------------------------------
    std::optional<size_t> skinned_node_index;
    for (size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const auto& node = asset.nodes[i];
        if (node.meshIndex.has_value() && node.skinIndex.has_value())
        {
            skinned_node_index = i;
            break;
        }
    }

    if (!skinned_node_index)
    {
        LogSkeletalMesh.Log("No skinned mesh node in %s", path.string().c_str());
        return std::nullopt;
    }

    const fastgltf::Node& skinned_node = asset.nodes[*skinned_node_index];
    const fastgltf::Skin& skin = asset.skins[*skinned_node.skinIndex];
    const fastgltf::Mesh& gltf_mesh = asset.meshes[*skinned_node.meshIndex];

    // ---------------------------------------------------------------
    // Node hierarchy
    // ---------------------------------------------------------------
    std::vector<int64_t> node_parent(asset.nodes.size(), -1);
    for (size_t i = 0; i < asset.nodes.size(); ++i)
        for (auto child : asset.nodes[i].children)
            node_parent[child] = (int64_t)i;

    std::unordered_map<size_t, uint32_t> joint_by_node;
    for (uint32_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
        joint_by_node[skin.joints[joint_index]] = joint_index;

    SkeletalMesh result;
    Skeleton& skeleton = result.skeleton;
    skeleton.bones.resize(skin.joints.size());

    std::optional<size_t> root_parent_node;

    for (uint32_t joint_index = 0; joint_index < skin.joints.size(); ++joint_index)
    {
        const size_t node_index = skin.joints[joint_index];
        const fastgltf::Node& node = asset.nodes[node_index];
        SkeletonBone& bone = skeleton.bones[joint_index];

        bone.name = node.name.c_str();
        get_node_trs(node, bone.bind_translation, bone.bind_rotation, bone.bind_scale);

        // nearest ancestor which is a joint
        int64_t parent = node_parent[node_index];
        while (parent >= 0 && !joint_by_node.contains((size_t)parent))
            parent = node_parent[parent];

        if (parent >= 0)
        {
            bone.parent = (int32_t)joint_by_node.at((size_t)parent);
        }
        else if (node_parent[node_index] >= 0 && !root_parent_node)
        {
            root_parent_node = (size_t)node_parent[node_index];
        }

        skeleton.bone_by_name[bone.name] = joint_index;
    }

    // Accumulate transforms of non-joint ancestors of the root bone(s)
    if (root_parent_node)
    {
        glm::mat4 root = glm::mat4(1.0f);
        int64_t current = (int64_t)*root_parent_node;
        while (current >= 0)
        {
            root = get_node_matrix(asset.nodes[current]) * root;
            current = node_parent[current];
        }
        skeleton.root_transform = root;
    }

    if (skin.inverseBindMatrices.has_value())
    {
        const auto& ibm_accessor = asset.accessors[*skin.inverseBindMatrices];
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(asset, ibm_accessor,
            [&](const fastgltf::math::fmat4x4& m, size_t index)
            {
                skeleton.bones[index].inverse_bind = fastgltf_to_glm(m);
            });
    }

    skeleton.build_evaluation_order();

    // ---------------------------------------------------------------
    // Geometry (reuses the static mesh path: vertices, tangents, winding flip)
    // ---------------------------------------------------------------
    std::vector<AssetSceneObject> objects;
    traverseNode(*skinned_node_index, asset, glm::mat4(1.0f), objects);
    checkf(!objects.empty(), "Skinned node produced no geometry");

    StaticMesh render_mesh = std::move(objects[0].mesh);

    // ---------------------------------------------------------------
    // Skin attributes
    // ---------------------------------------------------------------
    result.primitive_skins.resize(gltf_mesh.primitives.size());

    for (size_t prim_index = 0; prim_index < gltf_mesh.primitives.size(); ++prim_index)
    {
        const auto& primitive = gltf_mesh.primitives[prim_index];
        const size_t vertex_count = render_mesh.mesh_geometry[0].primitives[prim_index].vertices.size();

        std::vector<SkinVertex>& skin_vertices = result.primitive_skins[prim_index];
        skin_vertices.resize(vertex_count);

        std::vector<std::array<uint32_t, MAX_BONE_INFLUENCES>> joints(vertex_count);
        std::vector<std::array<float, MAX_BONE_INFLUENCES>> weights(vertex_count);
        for (auto& w : weights)
            w.fill(0.0f);
        for (auto& j : joints)
            j.fill(0);

        for (uint32_t set = 0; set < MAX_BONE_INFLUENCES / 4; ++set)
        {
            const std::string joints_name = "JOINTS_" + std::to_string(set);
            const std::string weights_name = "WEIGHTS_" + std::to_string(set);

            const auto* joints_attr = primitive.findAttribute(joints_name);
            const auto* weights_attr = primitive.findAttribute(weights_name);

            if (joints_attr == primitive.attributes.cend() || weights_attr == primitive.attributes.cend())
                continue;

            fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(asset,
                asset.accessors[joints_attr->accessorIndex],
                [&](const fastgltf::math::uvec4& j, size_t i)
                {
                    for (uint32_t c = 0; c < 4; ++c)
                        joints[i][set * 4 + c] = j[c];
                });

            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset,
                asset.accessors[weights_attr->accessorIndex],
                [&](const fastgltf::math::fvec4& w, size_t i)
                {
                    for (uint32_t c = 0; c < 4; ++c)
                        weights[i][set * 4 + c] = w[c];
                });
        }

        uint32_t unweighted = 0;
        for (size_t i = 0; i < vertex_count; ++i)
        {
            float sum = 0.0f;
            for (float w : weights[i])
                sum += w;

            if (sum <= 1e-6f)
            {
                // not skinned: bind rigidly to the first bone
                weights[i][0] = 1.0f;
                sum = 1.0f;
                unweighted++;
            }

            for (uint32_t c = 0; c < MAX_BONE_INFLUENCES; ++c)
            {
                checkf(joints[i][c] < skeleton.bones.size(), "Joint index out of range");
                weights[i][c] /= sum;
            }

            SkinVertex& sv = skin_vertices[i];
            sv.joints = glm::uvec4(
                pack_joints(joints[i][0], joints[i][1]),
                pack_joints(joints[i][2], joints[i][3]),
                pack_joints(joints[i][4], joints[i][5]),
                pack_joints(joints[i][6], joints[i][7]));
            sv.weights0 = glm::vec4(weights[i][0], weights[i][1], weights[i][2], weights[i][3]);
            sv.weights1 = glm::vec4(weights[i][4], weights[i][5], weights[i][6], weights[i][7]);
        }

        if (unweighted > 0)
            LogSkeletalMesh.Log("Primitive %zu: %u vertices without weights", prim_index, unweighted);
    }

    // ---------------------------------------------------------------
    // Validate: bind pose skinning matrices must be ~identity
    // ---------------------------------------------------------------
    {
        std::vector<glm::mat4> skinning;
        skeleton.compute_skinning_matrices(skeleton.make_bind_local_pose(), skinning);

        float max_error = 0.0f;
        for (const auto& m : skinning)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    max_error = std::max(max_error, std::abs(m[c][r] - (c == r ? 1.0f : 0.0f)));

        LogSkeletalMesh.Log("Loaded skeletal mesh %s: %u bones, %zu primitives, bind pose error %f",
            path.filename().string().c_str(), skeleton.num_bones(), result.primitive_skins.size(), max_error);
    }

    result.name = path.filename().string();
    result.render_mesh = AssetManager::get().store_mesh(std::move(render_mesh));

    return result;
}

const SkeletalMesh& SkeletalMeshHandle::get() const
{
    return AssetManager::get().get_skeletal_mesh(*this);
}

void serialize_json_value(SkeletalMeshHandle& target, const Json::Value& value, const SerializationContext& context)
{
    if (value.isString())
    {
        target = AssetManager::get().load_skeletal_mesh(value.asString());
    }
}
