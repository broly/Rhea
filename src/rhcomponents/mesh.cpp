module;

#include <meshoptimizer.h>

module rhcomponents;

import :mesh;
import :skinned_mesh;

import std.compat;
import assertions;
import fixed_string;
import ecs;
import rhmath;
import assets;
import render;
import physics;
import glm;
import log;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogSkinnedMesh, Log);
DEFINE_LOGGER(LogMeshCollision, Log);


namespace
{
    // bump when the cooking below changes: cached shapes are rebuilt
    constexpr uint32_t mesh_collision_cook_version = 2;

    // Render meshes are clockwise (the importer flips glTF's winding for the renderer, see traverseNode),
    // physics wants counter clockwise: Jolt tells convex from concave edges by the triangle normals, and with
    // them reversed the edge of a step counts as inactive and the character can't step onto it.
    // A mirroring scale reverses the winding once more.
    phys::TriangleMeshShape gather_triangles(const StaticMesh& mesh, glm::vec3 scale)
    {
        const bool mirrored = scale.x * scale.y * scale.z < 0.0f;
        phys::TriangleMeshShape triangles;
        for (const Geometry& geometry : mesh.mesh_geometry)
        {
            for (const Primitive& primitive : geometry.primitives)
            {
                const uint32_t base = (uint32_t)triangles.vertices.size();
                for (const Vertex& vertex : primitive.vertices)
                    triangles.vertices.push_back(vertex.position * scale);
                for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
                {
                    const uint32_t a = primitive.indices[i], b = primitive.indices[i + 1], c = primitive.indices[i + 2];
                    triangles.indices.push_back(base + (mirrored ? a : b));
                    triangles.indices.push_back(base + (mirrored ? b : a));
                    triangles.indices.push_back(base + c);
                }
            }
        }
        return triangles;
    }

    // Render vertices are split at UV / normal seams and between primitives: merge equal positions,
    // or the simplifier can't collapse across the seams and triangle edges don't connect
    void weld_vertices(phys::TriangleMeshShape& mesh)
    {
        std::vector<uint32_t> remap(mesh.vertices.size());
        const size_t unique = meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(),
                                                          mesh.vertices.data(), mesh.vertices.size(), sizeof(glm::vec3));
        std::vector<glm::vec3> vertices(unique);
        meshopt_remapVertexBuffer(vertices.data(), mesh.vertices.data(), mesh.vertices.size(), sizeof(glm::vec3), remap.data());
        std::vector<uint32_t> indices(mesh.indices.size());
        meshopt_remapIndexBuffer(indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
        mesh.vertices = std::move(vertices);
        mesh.indices = std::move(indices);
    }

    // Removes triangles while the surface moves at most max_error, and parts smaller than that
    void simplify(phys::TriangleMeshShape& mesh, float max_error)
    {
        std::vector<uint32_t> indices(mesh.indices.size());
        const size_t index_count = meshopt_simplify(indices.data(), mesh.indices.data(), mesh.indices.size(),
                                                    &mesh.vertices[0].x, mesh.vertices.size(), sizeof(glm::vec3),
                                                    0, max_error, meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPrune, nullptr);
        indices.resize(index_count);

        // drop the vertices no triangle uses any more
        std::vector<glm::vec3> vertices(mesh.vertices.size());
        const size_t vertex_count = meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(),
                                                                mesh.vertices.data(), mesh.vertices.size(), sizeof(glm::vec3));
        vertices.resize(vertex_count);
        mesh.vertices = std::move(vertices);
        mesh.indices = std::move(indices);
    }

    phys::BoxShape bounding_box(const phys::TriangleMeshShape& mesh)
    {
        glm::vec3 min(std::numeric_limits<float>::max());
        glm::vec3 max(std::numeric_limits<float>::lowest());
        for (const glm::vec3& v : mesh.vertices)
        {
            min = glm::min(min, v);
            max = glm::max(max, v);
        }
        // planes (decals, floors of a single quad) still get some thickness
        return { .half_extents = glm::max(0.5f * (max - min), glm::vec3(0.01f)), .center = 0.5f * (min + max) };
    }
}

phys::Shape cook_mesh_collision(phys::PhysicsScene& physics, const StaticMesh& mesh, glm::vec3 scale,
                                const MeshCollisionSettings& settings, std::string_view cache_key)
{
    if (settings.type == MeshCollision::none)
        return {};

    phys::TriangleMeshShape triangles = gather_triangles(mesh, scale);
    if (triangles.indices.size() < 3)
        return {};

    uint64_t hash = phys::hash_mesh(phys::shape_hash_seed, triangles);
    hash = phys::hash_value(hash, mesh_collision_cook_version);
    hash = phys::hash_value(hash, settings.type);
    if (settings.type == MeshCollision::simplified)
        hash = phys::hash_value(hash, settings.simplify_error);
    if (phys::Shape cached = physics.load_cached_shape(cache_key, hash))
        return cached;

    const size_t source_triangles = triangles.indices.size() / 3;
    weld_vertices(triangles);

    phys::Shape shape;
    switch (settings.type)
    {
    case MeshCollision::none:
        break;
    case MeshCollision::box:
        shape = physics.create_shape(bounding_box(triangles));
        break;
    case MeshCollision::convex_hull:
        shape = physics.create_shape(phys::ConvexHullShape{ triangles.vertices });
        if (!shape)
            shape = physics.create_shape(bounding_box(triangles));
        break;
    case MeshCollision::simplified:
        simplify(triangles, settings.simplify_error);
        LogMeshCollision.Log("'%.*s': simplified %zu -> %zu triangles", (int)cache_key.size(), cache_key.data(),
            source_triangles, triangles.indices.size() / 3);
        shape = physics.create_mesh_shape(triangles);
        break;
    case MeshCollision::triangles:
        shape = physics.create_mesh_shape(triangles);
        break;
    }

    if (shape)
        physics.store_cached_shape(cache_key, hash, shape);
    return shape;
}

AABB get_mesh_bounds(const MeshRenderer& renderer, const Transform& world)
{
    if (!renderer.mesh.is_valid())
        return AABB::zero();
    return renderer.mesh.get().bounds * world;
}

void SkinnedMesh::set_local_pose(const std::vector<glm::mat4>& in_local_pose)
{
    checkf(in_local_pose.size() == get_skeleton().num_bones(), "Pose size mismatch");
    local_pose = in_local_pose;
    get_skeleton().compute_skinning_matrices(local_pose, pose->skinning_matrices);
    pose->version++;
}

void SkinnedMesh::reset_to_bind_pose()
{
    set_local_pose(get_skeleton().make_bind_local_pose());
}

void SkinnedMesh::set_morph_weights(const std::vector<float>& weights)
{
    checkf(weights.size() == pose->morph_weights.size(), "Morph weight count mismatch");
    if (weights == pose->morph_weights)
        return;
    pose->morph_weights = weights;
    pose->version++;
}

void init_skinned_mesh(ecs::Registry& registry, ecs::Entity e)
{
    SkinnedMesh* skinned = registry.get<SkinnedMesh>(e);
    checkf(skinned && skinned->skeletal_mesh.is_valid(), "init_skinned_mesh: no skeletal mesh");
    const SkeletalMesh& skeletal = skinned->skeletal_mesh.get();

    skinned->pose = std::make_shared<SkinningPose>();
    skinned->pose->mesh = skinned->skeletal_mesh;
    skinned->pose->morph_weights.assign(skeletal.num_morph_targets(), 0.0f);
    skinned->reset_to_bind_pose();

    MeshRenderer renderer = registry.has<MeshRenderer>(e) ? *registry.get<MeshRenderer>(e) : MeshRenderer{};
    renderer.mesh = skeletal.render_mesh;
    const size_t num_primitives = renderer.mesh.get().mesh_geometry[0].primitives.size();
    if (renderer.materials.size() < num_primitives)
        LogSkinnedMesh.Log("Skinned mesh '%s': %zu materials provided, mesh has %zu primitives",
            skeletal.name.c_str(), renderer.materials.size(), num_primitives);
    registry.add<MeshRenderer>(e, std::move(renderer));
}
