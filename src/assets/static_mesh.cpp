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

    StaticMesh mesh;
    
    for (auto& gltf_node : asset.nodes)
    {
        if (!gltf_node.meshIndex.has_value())
            continue;
        
        auto trs = std::get<fastgltf::TRS>(gltf_node.transform);
        Transform t {
                fastgltf_to_glm(trs.translation),
                fastgltf_to_glm(trs.rotation),
                fastgltf_to_glm(trs.scale),
        };
        MeshNode node;
        node.index = *gltf_node.meshIndex;
        node.transform = t;
        mesh.nodes.push_back(node);
    }
    
    for (auto& gltf_material : asset.materials)
    {
        mesh.material_names.push_back(gltf_material.name.c_str());
    }
    
    for (auto& gltf_mesh : asset.meshes)
    {
        Geometry mesh_section;
        
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
                    v.position = fastgltf_to_glm(position);
                    v.position.z *= -1;

                    mesh.bounds.min = glm::min(mesh.bounds.min, fastgltf_to_glm(position));
                    mesh.bounds.max = glm::max(mesh.bounds.max, fastgltf_to_glm(position));
                }
            );

            if (normAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, *normAccessor,
                    [&](fastgltf::math::fvec3 normal, size_t index)
                    {
                        mesh_primitive.vertices[index].normal = fastgltf_to_glm(normal);
                    }
                );
            }
        
            if (tangentAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, *tangentAccessor,
                    [&](fastgltf::math::fvec4 tangent, size_t index)
                    {
                        mesh_primitive.vertices[index].tangent = fastgltf_to_glm(tangent);
                    }
                );
            }


            if (uvAccessor) {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, *uvAccessor,
                    [&](fastgltf::math::fvec2 uv, size_t index)
                    {
                        mesh_primitive.vertices[index].tex_coord = fastgltf_to_glm(uv);
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
        
        mesh.mesh_geometry.push_back(mesh_section);
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
