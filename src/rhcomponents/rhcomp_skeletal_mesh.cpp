module rhcomponents:rhcomp_skeletal_mesh;

import log;

#include "common/assertion_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogSkeletalMeshComponent, Log);


void RhComp_SkeletalMesh::on_serialize(const SerializationContext& context)
{
    RhComp_StaticMesh::on_serialize(context);

    checkf(skeletal_mesh.is_valid(), "RhComp_SkeletalMesh '%s': skeletal mesh is not loaded", name.to_string().c_str());

    const SkeletalMesh& skeletal = skeletal_mesh.get();

    mesh = skeletal.render_mesh;

    const size_t num_primitives = mesh.get().mesh_geometry[0].primitives.size();
    checkf(mats.size() >= num_primitives,
        "RhComp_SkeletalMesh '%s': %zu materials provided, mesh has %zu primitives",
        name.to_string().c_str(), mats.size(), num_primitives);

    pose = std::make_shared<SkinningPose>();
    pose->mesh = skeletal_mesh;

    reset_to_bind_pose();
}

void RhComp_SkeletalMesh::update_scene_proxy()
{
    RhComp_StaticMesh::update_scene_proxy();
    scene_proxy.skinning = pose;
}

void RhComp_SkeletalMesh::set_local_pose(const std::vector<glm::mat4>& in_local_pose)
{
    checkf(in_local_pose.size() == skeletal_mesh.get().skeleton.num_bones(), "Pose size mismatch");
    local_pose = in_local_pose;
    rebuild_skinning_matrices();
}

void RhComp_SkeletalMesh::reset_to_bind_pose()
{
    local_pose = skeletal_mesh.get().skeleton.make_bind_local_pose();
    rebuild_skinning_matrices();
}

void RhComp_SkeletalMesh::rebuild_skinning_matrices()
{
    skeletal_mesh.get().skeleton.compute_skinning_matrices(local_pose, pose->skinning_matrices);
    pose->version++;
}
