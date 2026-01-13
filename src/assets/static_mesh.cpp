module;

module assets:mesh;

import <iostream>;
import glm;
import <json/value.h>;

import engine;
import globals;
import dependency_collector;
import fastgltf;
import fastgltf_helper;
import :helpers;

std::optional<StaticMesh> StaticMesh::create_from_file(const std::filesystem::path path)
{
    static constexpr auto supported_extensions =
        fastgltf::Extensions::KHR_mesh_quantization |
        fastgltf::Extensions::KHR_texture_transform |
        fastgltf::Extensions::KHR_materials_variants;

    fastgltf::Parser parser(supported_extensions);

    constexpr auto gltf_options =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::AllowDouble |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::LoadExternalImages |
        fastgltf::Options::GenerateMeshIndices;

    auto gltfFile = fastgltf::MappedGltfFile::FromPath(path);
    if (!gltfFile) {
        std::cerr << "Failed to open glTF file\n";
        return std::nullopt;
    }

    auto assetResult = parser.loadGltf(
        gltfFile.get(),
        path.parent_path(),
        gltf_options
    );

    if (assetResult.error() != fastgltf::Error::None) {
        std::cerr << "Failed to load glTF\n";
        return std::nullopt;
    }

    fastgltf::Asset& asset = assetResult.get();

    if (asset.meshes.empty()) {
        std::cerr << "No meshes in glTF\n";
        return std::nullopt;
    }

    std::vector<AssetSceneObject> result_meshes;
    
    
    // glTF (Y-up) → Engine (Z-up)
    glm::mat4 gltfToEngine =
        glm::rotate(glm::mat4(1.0f),
                    glm::radians(-90.0f),
                    glm::vec3(1, 0, 0));

    const auto& scene = asset.scenes[0];
    glm::mat4 root = glm::mat4(1.0f);

    for (auto node_index : scene.nodeIndices)
    {
        traverseNodeBake(
            node_index,
            asset,
            root,
            result_meshes);
    }
    
    StaticMesh mesh = std::move(result_meshes[0].mesh);
   
    for (auto& gltf_material : asset.materials)
    {
        mesh.material_names.push_back(gltf_material.name.c_str());
    }
    return mesh;
}

const StaticMesh& MeshHandle::get() const
{
    return RhGlobals::engine->asset_manager->get_mesh(*this);
}

const Primitive& MeshPrimHandle::get() const
{
    return mesh.get().mesh_geometry[geom_index].primitives[prim_index];
}

void serialize_json_value(MeshHandle& target, const Json::Value& value, DependencyCollector* dc)
{
    if (value.isString())
    {
        std::string path = value.asString();
        target = RhGlobals::engine->asset_manager->load_mesh(path);
    }
}
