export module assets:helpers;

import <iostream>;
import glm;
import <json/value.h>;
import dependency_collector;
import fastgltf;
import fastgltf_helper;
import :types;
import :mesh;


static glm::mat4 getLocalMatrix(const fastgltf::Node& node)
{
    if (std::holds_alternative<fastgltf::TRS>(node.transform)) {
        const auto& trs = std::get<fastgltf::TRS>(node.transform);
        return
            glm::translate(glm::mat4(1.0f), fastgltf_to_glm(trs.translation)) *
            glm::mat4_cast(fastgltf_to_glm(trs.rotation)) *
            glm::scale(glm::mat4(1.0f), fastgltf_to_glm(trs.scale));
    }

    if (std::holds_alternative<fastgltf::math::fmat4x4>(node.transform)) {
        return fastgltf_to_glm(
            std::get<fastgltf::math::fmat4x4>(node.transform));
    }

    return glm::mat4(1.0f);
}


export void traverseNodeBake(
    size_t nodeIndex,
    const fastgltf::Asset& asset,
    const glm::mat4& parentMatrix,
    std::vector<AssetSceneObject>& outObjects)
{
    const auto& node = asset.nodes[nodeIndex];

    glm::mat4 local = getLocalMatrix(node);
    glm::mat4 world = parentMatrix * local;

    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(world)));

    if (node.meshIndex.has_value())
    {
        AssetSceneObject obj;
        obj.transform = Transform::make_identity();

        const auto& gltfMesh = asset.meshes[*node.meshIndex];
        auto& mesh = obj.mesh;
        mesh.name = gltfMesh.name.c_str();

        mesh.mesh_geometry.push_back({});
        Geometry& geom = mesh.mesh_geometry.back();

        for (const auto& primitive : gltfMesh.primitives)
        {
            Primitive prim;

            if (primitive.materialIndex) {
                mesh.material_names.push_back(
                    asset.materials[*primitive.materialIndex].name.c_str());
                prim.material_index = mesh.material_names.size() - 1;
            }

            const auto& posAcc =
                asset.accessors.at(
                    primitive.findAttribute("POSITION")->accessorIndex);

            const auto* normIt = primitive.findAttribute("NORMAL");
            const auto* tanIt  = primitive.findAttribute("TANGENT");
            const auto* uvIt   = primitive.findAttribute("TEXCOORD_0");

            const fastgltf::Accessor* normAcc =
                normIt ? &asset.accessors[normIt->accessorIndex] : nullptr;
            const fastgltf::Accessor* tanAcc =
                tanIt ? &asset.accessors[tanIt->accessorIndex] : nullptr;
            const fastgltf::Accessor* uvAcc =
                uvIt ? &asset.accessors[uvIt->accessorIndex] : nullptr;

            prim.vertices.resize(posAcc.count);

            mesh.bounds.min = glm::vec3(std::numeric_limits<float>::max());
            mesh.bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

            // POSITIONS
            fastgltf::iterateAccessorWithIndex<
                fastgltf::math::fvec3>(asset, posAcc,
                [&](auto p, size_t i)
                {
                    glm::vec3 wp =
                        glm::vec3(world * glm::vec4(
                            fastgltf_to_glm(p), 1.0f));

                    prim.vertices[i].position = wp;
                    mesh.bounds.min = glm::min(mesh.bounds.min, wp);
                    mesh.bounds.max = glm::max(mesh.bounds.max, wp);
                });

            
            // NORMALS
            if (normAcc) {
                fastgltf::iterateAccessorWithIndex<
                    fastgltf::math::fvec3>(asset, *normAcc,
                    [&](auto n, size_t i)
                    {
                        prim.vertices[i].normal =
                            glm::normalize(
                                normalMatrix * fastgltf_to_glm(n));
                    });
            }
            
            
            
            // TANGENTS
            if (tanAcc) {
                fastgltf::iterateAccessorWithIndex<
                    fastgltf::math::fvec4>(asset, *tanAcc,
                    [&](auto t, size_t i)
                    {
                        glm::vec3 tan =
                            normalMatrix * glm::vec3(
                                fastgltf_to_glm(t));
                        prim.vertices[i].tangent = glm::vec4(glm::normalize(tan), t.w());
                        prim.vertices[i].tangent = glm::vec4(t.x(), t.y(), t.z(), t.w());
                    });
            }

            // UV
            if (uvAcc) {
                fastgltf::iterateAccessorWithIndex<
                    fastgltf::math::fvec2>(asset, *uvAcc,
                    [&](auto uv, size_t i)
                    {
                        prim.vertices[i].tex_coord =
                            fastgltf_to_glm(uv);
                    });
            }

            // INDICES
            if (primitive.indicesAccessor) {
                const auto& ia =
                    asset.accessors[*primitive.indicesAccessor];
                prim.indices.reserve(ia.count);

                fastgltf::iterateAccessor<uint32_t>(
                    asset, ia,
                    [&](uint32_t idx)
                    {
                        prim.indices.push_back(idx);
                    });
            }

            geom.primitives.push_back(std::move(prim));
            
        }
        
        
        // float det = glm::determinant(glm::mat3(world));
        // bool mirrored = (det < 0.0f);
        //
        // if (mirrored) 
        {
            for (auto& p : geom.primitives) {
                for (size_t i = 0; i < p.indices.size(); i += 3) {
                    std::swap(p.indices[i], p.indices[i + 1]);
                }
            }
        }

        outObjects.push_back(std::move(obj));
    }

    for (auto child : node.children)
        traverseNodeBake(child, asset, world, outObjects);
}
