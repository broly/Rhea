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


phys::Shape cook_mesh_collision(phys::PhysicsScene& physics, const StaticMesh& mesh, glm::vec3 scale, std::string_view cache_key)
{
    phys::TriangleMeshShape triangles;
    for (const Geometry& geometry : mesh.mesh_geometry)
    {
        for (const Primitive& primitive : geometry.primitives)
        {
            const uint32_t base = (uint32_t)triangles.vertices.size();
            for (const Vertex& vertex : primitive.vertices)
                triangles.vertices.push_back(vertex.position * scale);
            for (uint32_t index : primitive.indices)
                triangles.indices.push_back(base + index);
        }
    }
    return physics.create_mesh_shape(triangles, cache_key);
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
