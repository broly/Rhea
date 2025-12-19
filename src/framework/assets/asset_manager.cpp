#include "asset_manager.h"

#include <iostream>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <glm/ext/vector_common.hpp>

#include "engine.h"
#include "globals.h"
#include "mesh.h"
#include "common/paths.h"



template<typename T>
static auto fastgtlf_to_glm(const T& v)
{
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fvec3>)
    {
        return glm::vec3(v.x(), v.y(), v.z());
    } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, fastgltf::math::fvec2>)
    {
        return glm::vec2(v.x(), v.y());
    }
}

MeshHandle AssetManager::load_mesh(const std::string& rel_path)
{
    if (mesh_by_path.contains(rel_path))
        return mesh_by_path[rel_path];
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;

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
        return MeshHandle::invalid();
    }

    auto assetResult = parser.loadGltf(
        gltfFile.get(),
        path.parent_path(),
        gltf_options
    );

    if (assetResult.error() != fastgltf::Error::None) {
        std::cerr << "Failed to load glTF\n";
        return MeshHandle::invalid();
    }

    fastgltf::Asset& asset = assetResult.get();

    if (asset.meshes.empty()) {
        std::cerr << "No meshes in glTF\n";
        return MeshHandle::invalid();
    }

    Mesh mesh;

    const fastgltf::Mesh& gltfMesh = asset.meshes[0];
    const fastgltf::Primitive& primitive = gltfMesh.primitives[0];

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
        tangentIt ? &asset.accessors.at(normIt->accessorIndex) : nullptr;
    const fastgltf::Accessor* uvAccessor =
        uvIt ? &asset.accessors.at(uvIt->accessorIndex) : nullptr;

    mesh.vertices.resize(posAccessor.count);

    mesh.bounds.min = glm::vec3(std::numeric_limits<float>::max());
    mesh.bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, posAccessor,
        [&](fastgltf::math::fvec3 position, size_t index)
        {
            Vertex& v = mesh.vertices[index];
            v.position = fastgtlf_to_glm(position);

            mesh.bounds.min = glm::min(mesh.bounds.min, fastgtlf_to_glm(position));
            mesh.bounds.max = glm::max(mesh.bounds.max, fastgtlf_to_glm(position));
        }
    );

    if (normAccessor) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, *normAccessor,
            [&](fastgltf::math::fvec3 normal, size_t index)
            {
                mesh.vertices[index].normal = fastgtlf_to_glm(normal);
            }
        );
    }
    
    if (tangentAccessor) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, *tangentAccessor,
            [&](fastgltf::math::fvec3 normal, size_t index)
            {
                mesh.vertices[index].tangent = fastgtlf_to_glm(normal);
            }
        );
    }


    if (uvAccessor) {
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, *uvAccessor,
            [&](fastgltf::math::fvec2 uv, size_t index)
            {
                mesh.vertices[index].tex_coord = fastgtlf_to_glm(uv);
            }
        );
    }

    /* =========================
       INDICES
       ========================= */

    if (primitive.indicesAccessor.has_value()) {
        const auto& indexAccessor =
            asset.accessors.at(*primitive.indicesAccessor);

        mesh.indices.reserve(indexAccessor.count);

        fastgltf::iterateAccessor<uint32_t>(
            asset,
            indexAccessor,
            [&](uint32_t index)
            {
                mesh.indices.push_back(index);
            }
        );
    }
    
    
    mesh.name = rel_path;
    const uint32_t mesh_id = meshes_counter++;
    
    MeshHandle mesh_handle {mesh_id};
    
    mesh_by_path.insert({rel_path, mesh_handle});
    loaded_meshes.insert({mesh_handle, std::move(mesh)});

    return mesh_handle;
}

AssetManager& AssetManager::get()
{
    return *RhGlobals::engine->asset_manager;
}

const Mesh& AssetManager::get_mesh(MeshHandle id)
{
    return loaded_meshes[id];
}
