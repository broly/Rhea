module assets:asset_scene;

import <iostream>;
import glm;
import <json/value.h>;
import engine;
import globals;
import dependency_collector;
import fastgltf;
import fastgltf_helper;
import :helpers;
#include "common/assertion_macros.h"

AssetSceneInfo AssetSceneHandle::load(const std::filesystem::path& path, const std::string& rel_textures_path)
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
        fastgltf::Options::GenerateMeshIndices;

    auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
    if (!gltfFile) {
        std::cerr << "Failed to open glTF file\n";
        return {};
    }

    auto assetResult = parser.loadGltf(
        gltfFile.get(),
        path.parent_path(),
        gltf_options
    );

    if (assetResult.error() != fastgltf::Error::None) {
        std::cerr << "Failed to load glTF\n";
        return {};
    }

    fastgltf::Asset& asset = assetResult.get();

    if (asset.meshes.empty()) {
        std::cerr << "No meshes in glTF\n";
        return {};
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
        traverseNode(
            node_index,
            asset,
            root,
            result_meshes);
    }
    
    std::map<std::string, std::shared_ptr<Material>> result_materials;
    
    for (auto& scene_mat : asset.materials)
    {
        std::shared_ptr<Material> mat = new_object<Material>();
        mat->model = "pbr";
        switch (scene_mat.alphaMode)
        {
        case fastgltf::AlphaMode::Opaque:
            mat->parameters["blend_mode"] = "opaque";
            break;
        case fastgltf::AlphaMode::Mask:
            mat->parameters["blend_mode"] = "masked";
            break;
        case fastgltf::AlphaMode::Blend:
            mat->parameters["blend_mode"] = "translucent";
            break;
        default: ;
        }
        if (scene_mat.pbrData.baseColorTexture.has_value())
        {
            auto& texture_info = scene_mat.pbrData.baseColorTexture.value();
            std::string texture_name = asset.textures[texture_info.textureIndex].name.c_str();
            auto full_texture_path = rel_textures_path + "/" + texture_name;
            mat->parameters["base_color"] = TextureHandle::make_pending(full_texture_path);
            LinearColor base_color_factor = {scene_mat.pbrData.baseColorFactor.x(), scene_mat.pbrData.baseColorFactor.y(), scene_mat.pbrData.baseColorFactor.z(), 1.0f};
            mat->parameters["base_color_factor"] = base_color_factor;
        }
        else
        {
            mat->parameters["base_color_factor"] = LinearColor(1.0, 1.0, 1.0, 1.0);
            mat->parameters["base_color"] = TextureHandle::invalid();
        }
        if (scene_mat.pbrData.metallicRoughnessTexture.has_value())
        {
            auto& texture_info = scene_mat.pbrData.metallicRoughnessTexture.value();
            std::string texture_name = asset.textures[texture_info.textureIndex].name.c_str();
            auto full_texture_path = rel_textures_path + "/" + texture_name;
            mat->parameters["orm"] = TextureHandle::make_pending(full_texture_path);
            const float rv = scene_mat.pbrData.roughnessFactor;
            const float mv = scene_mat.pbrData.metallicFactor;
            mat->parameters["roughness_factor"] = LinearColor(rv, rv, rv, 1.0);
            mat->parameters["metallic_factor"] = LinearColor(mv, mv, mv, 1.0);
        }
        else
        {
            mat->parameters["roughness_factor"] = LinearColor(1.0, 1.0, 1.0, 1.0);
            mat->parameters["metallic_factor"] = LinearColor(1.0, 1.0, 1.0, 1.0);
            mat->parameters["orm"] = TextureHandle::invalid();
        }
        if (scene_mat.normalTexture.has_value())
        {
            auto& texture_info = scene_mat.normalTexture.value();
            std::string texture_name = asset.textures[texture_info.textureIndex].name.c_str();
            auto full_texture_path = rel_textures_path + "/" + texture_name;
            mat->parameters["normal"] = TextureHandle::make_pending(full_texture_path);
        }
        else
        {
            mat->parameters["normal"] = TextureHandle::invalid();
        }
        if (scene_mat.emissiveTexture.has_value())
        {
            auto& texture_info = scene_mat.emissiveTexture.value();
            std::string texture_name = asset.textures[texture_info.textureIndex].name.c_str();
            auto full_texture_path = rel_textures_path + "/" + texture_name;
            mat->parameters["emissive"] = TextureHandle::make_pending(full_texture_path);
            LinearColor emissive_factor = {scene_mat.emissiveFactor.x(), scene_mat.emissiveFactor.y(), scene_mat.emissiveFactor.z(), 1.0f};
            mat->parameters["emissive_factor"] = emissive_factor;
        }
        else
        {
            mat->parameters["emissive_factor"] = LinearColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat->parameters["emissive"] = TextureHandle::invalid();
        }
        std::string mat_name = scene_mat.name.c_str();
        result_materials.insert({mat_name, mat});
    }
    
    return {std::move(result_meshes), std::move(result_materials) };
}
