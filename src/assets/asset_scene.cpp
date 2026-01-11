module assets:asset_scene;

import <iostream>;
import glm;
import <json/value.h>;

import engine;
import globals;
import dependency_collector;
import fastgltf;
import fastgltf_helper;

std::vector<AssetSceneObject> AssetSceneHandle::load(const std::filesystem::path& path)
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

    // StaticMesh mesh;
    
    std::vector<AssetSceneObject> result;
    
    for (auto& gltf_node : asset.nodes)
    {
        if (!gltf_node.meshIndex.has_value())
            continue;
        
        auto mesh_index = *gltf_node.meshIndex;
        
        auto trs = std::get<fastgltf::TRS>(gltf_node.transform);
        Transform t {
                fastgtlf_to_glm(trs.translation),
                fastgtlf_to_glm(trs.rotation),
                fastgtlf_to_glm(trs.scale),
        };
        
        result.push_back({});
        
        auto& scene_object = result.back();
        scene_object.transform = t;
        
        
        auto& mesh = scene_object.mesh;
        auto& gltf_mesh = asset.meshes[mesh_index];

        scene_object.mesh.mesh_geometry.push_back({});
        Geometry& mesh_section = mesh.mesh_geometry.back();
        
        for (auto& primitive : gltf_mesh.primitives)
        {
            Primitive mesh_primitive;
            if (primitive.materialIndex.has_value())
                mesh_primitive.material_index = primitive.materialIndex.value();
            /* =========================
               VERTICES
               ========================= */

            const auto& posAccessor = asset.accessors.at(
                primitive.findAttribute("POSITION")->accessorIndex
            );

            const auto* normIt = primitive.findAttribute("NORMAL");
            const auto* tangentIt = primitive.findAttribute("TANGENT");
            const auto* uvIt   = primitive.findAttribute("TEXCOORD_0");

            const fastgltf::Accessor* normAccessor =
                normIt ? &asset.accessors.at(normIt->accessorIndex) : nullptr;
            const fastgltf::Accessor* tangentAccessor =
                tangentIt != primitive.attributes.end() ? &asset.accessors.at(tangentIt->accessorIndex) : nullptr;
            const fastgltf::Accessor* uvAccessor =
                uvIt ? &asset.accessors.at(uvIt->accessorIndex) : nullptr;

            mesh_primitive.vertices.resize(posAccessor.count);

            mesh.bounds.min = glm::vec3(std::numeric_limits<float>::max());
            mesh.bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, posAccessor,
                [&](fastgltf::math::fvec3 position, size_t index)
                {
                    Vertex& v = mesh_primitive.vertices[index];
                    v.position = fastgtlf_to_glm(position);
                    v.position.z *= -1;

                    mesh.bounds.min = glm::min(mesh.bounds.min, fastgtlf_to_glm(position));
                    mesh.bounds.max = glm::max(mesh.bounds.max, fastgtlf_to_glm(position));
                }
            );

            if (normAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, *normAccessor,
                    [&](fastgltf::math::fvec3 normal, size_t index)
                    {
                        mesh_primitive.vertices[index].normal = fastgtlf_to_glm(normal);
                    }
                );
            }
        
            if (tangentAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, *tangentAccessor,
                    [&](fastgltf::math::fvec4 tangent, size_t index)
                    {
                        mesh_primitive.vertices[index].tangent = fastgtlf_to_glm(tangent);
                    }
                );
            }


            if (uvAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, *uvAccessor,
                    [&](fastgltf::math::fvec2 uv, size_t index)
                    {
                        mesh_primitive.vertices[index].tex_coord = fastgtlf_to_glm(uv);
                    }
                );
            }

            /* =========================
               INDICES
               ========================= */

            if (primitive.indicesAccessor.has_value())
            {
                const auto& indexAccessor =
                    asset.accessors.at(*primitive.indicesAccessor);

                mesh_primitive.indices.reserve(indexAccessor.count);

                fastgltf::iterateAccessor<uint32_t>(
                    asset,
                    indexAccessor,
                    [&](uint32_t index)
                    {
                        mesh_primitive.indices.push_back(index);
                    }
                );
            }
            mesh_section.primitives.push_back(mesh_primitive);
        }
        
    }
    
    return result;
}
